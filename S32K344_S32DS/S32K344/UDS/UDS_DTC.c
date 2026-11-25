/*
 * UDS_DTC.c
 *
 *  DTC memory: status byte, extended data, freeze frame, ageing and the NVM
 *  store.  See UDS_DTC.h for the request API and UDS_DemCfg.h for the sizing
 *  and the NVM access rules.
 *
 *  NVM store (NVM block 2, payload offset, see Nvm.h / NvmApp.h):
 *
 *      offset 0      store version (DEM_STORE_VERSION)
 *      offset 1..2   operation cycle counter, big endian
 *      offset 3..    DEM_NVM_SLOT_NUM records of DEM_DTC_RECORD_SIZE bytes:
 *                        DTC code       3 bytes, big endian
 *                        status         1 byte (persistent bits only)
 *                        occurrences    1 byte
 *                        failure cycle  2 bytes, big endian
 *                        freeze frame   DEM_SNAPSHOT_DATA_SIZE bytes
 *
 *  The store is a pool of slots, not one slot per configured DTC: only the
 *  DTCs that are confirmed occupy a record, so the DTC/fault tables may be far
 *  longer than the store (a BMS with more than 100 faults still needs only a
 *  handful of records).  A new DTC takes a free slot, or the slot of the
 *  oldest DTC that is not failing at the moment when the store is full.
 *
 *  Ageing uses the operation cycle counter instead of a per-DTC counter that
 *  would have to be rewritten every power-on: a record stores the cycle it
 *  failed in, and the age is (current cycle - failure cycle).
 *
 *  Every write goes to the NVM shadow of NvmApp and is committed by
 *  Nvm_UserBlock_Polling_10ms() in the 10 ms task, so all records that changed
 *  between two commits cost one DFlash commit.  Nvm_WriteByte() ignores a byte
 *  that is already stored, so writing an unchanged field costs nothing.
 */

#include "UDS_DTC.h"
#include "UDS_DemCfg.h"
#include "Nvm.h"
#include "NvmApp.h"
#include <string.h>

/* DTC code of a slot that is not in use (erased DFlash reads 0xFF). */
#define DEM_DTC_CODE_UNUSED     (0xFFFFFFUL)

/* Index returned by DemDtcFindIndex() when the DTC is not configured. */
#define DEM_INDEX_NONE          (0xFFFFU)

/* Status byte of a DTC that was never tested since the last clear. */
#define DEM_STATUS_INITIAL      (0x50U)   /* testNotCompletedSinceLastClear |   */
                                          /* testNotCompletedThisOperationCycle */

/*
 * The whole status byte is stored and restored (ISO 14229-1, D.2: testFailed
 * keeps the most recent result across an operation cycle, the other bits are
 * either latched until clear or describe the cycle).  Only the per-cycle bits
 * are re-initialized by DemDtcNewCycle() at start-up, see D.4 / D.5 / D.9.
 */

/* The DTC store has to fit into one NVM block. */
typedef char UDS_DtcStoreFitsCheck[
	((DEM_STORE_HEADER_SIZE + (DEM_NVM_SLOT_NUM * DEM_DTC_RECORD_SIZE)) <= NVM_BLOCK_DATA_SIZE)
	? 1 : -1];

/* The slot index is kept in one byte. */
typedef char UDS_DtcSlotCountCheck[
	((DEM_NVM_SLOT_NUM >= 1U) && (DEM_NVM_SLOT_NUM < 255U)) ? 1 : -1];

/* DTC table and runtime state. */
static DtcEntryStruct* DtcHandle = NULL_PTR;
static unsigned int DtcHandleLen = 0U;
static unsigned char DtcInitDone = 0U;
static unsigned char DtcSettingOff = 0U;

/* Operation cycle counter (NVM) and the flag of a failed header write. */
static unsigned short DemOpCycle = 0U;
static unsigned char DemCycleWritePending = 0U;

/* Number of records found in the store during start-up. */
static unsigned int DemRestoredCount = 0U;

/* Table helpers used by the slot handling below. */
static unsigned int DemDtcFindIndex(unsigned int DtcCode);
static void DemDtcClearRuntime(DtcEntryStruct* Dtc);

/* ------------------------------------------------------------------------- */
/* NVM store: low level access                                               */
/* ------------------------------------------------------------------------- */

static unsigned int DemSlotOffset(unsigned int Slot)
{
	return (unsigned int)DEM_STORE_OFFSET + DEM_STORE_HEADER_SIZE +
	       (Slot * DEM_DTC_RECORD_SIZE);
}

static uint8 DemNvmRead(unsigned int Offset)
{
	return Nvm_Block2_Read_U8((Block2Data)Offset);
}

static Std_ReturnType DemNvmWrite(unsigned int Offset, uint8 Value)
{
	/* Nvm_WriteByte() ignores a byte that already has this value, so this is
	 * also the "write only what changed" path. */
	return Nvm_Block2_Write_U8((Block2Data)Offset, Value);
}

/* ------------------------------------------------------------------------- */
/* NVM store: header (version + operation cycle)                             */
/* ------------------------------------------------------------------------- */

static unsigned char DemStoreVersionOk(void)
{
	return ((uint8)DEM_STORE_VERSION == DemNvmRead((unsigned int)DEM_STORE_OFFSET)) ? 1U : 0U;
}

static unsigned short DemCycleRead(void)
{
	return (unsigned short)(((unsigned short)DemNvmRead((unsigned int)DEM_STORE_OFFSET + 1U) << 8) |
	                         (unsigned short)DemNvmRead((unsigned int)DEM_STORE_OFFSET + 2U));
}

static Std_ReturnType DemHeaderWrite(void)
{
	Std_ReturnType ret;

	ret = DemNvmWrite((unsigned int)DEM_STORE_OFFSET, (uint8)DEM_STORE_VERSION);
	if (E_OK == ret)
	{
		ret = DemNvmWrite((unsigned int)DEM_STORE_OFFSET + 1U,
		                  (uint8)((DemOpCycle >> 8) & 0xFFU));
	}
	if (E_OK == ret)
	{
		ret = DemNvmWrite((unsigned int)DEM_STORE_OFFSET + 2U,
		                  (uint8)(DemOpCycle & 0xFFU));
	}

	return ret;
}

/* Write the header and remember whether it has to be retried. */
static void DemCycleWrite(void)
{
	if (FALSE == Nvm_IsInitialised())
	{
		DemCycleWritePending = 1U;
		return;
	}

	DemCycleWritePending = (E_OK == DemHeaderWrite()) ? 0U : 1U;
}

/* ------------------------------------------------------------------------- */
/* NVM store: slots                                                          */
/* ------------------------------------------------------------------------- */

static unsigned int DemSlotCodeRead(unsigned int Slot)
{
	unsigned int off = DemSlotOffset(Slot);

	return ((unsigned int)DemNvmRead(off) << 16) |
	       ((unsigned int)DemNvmRead(off + 1U) << 8) |
	        (unsigned int)DemNvmRead(off + 2U);
}

/* A slot is free when it was erased, or when it holds a DTC that is no longer
 * configured (left over from an older DTC table). */
static unsigned char DemSlotIsFree(unsigned int Slot)
{
	unsigned int code = DemSlotCodeRead(Slot);

	if (DEM_DTC_CODE_UNUSED == code)
	{
		return 1U;
	}

	return (DEM_INDEX_NONE == DemDtcFindIndex(code)) ? 1U : 0U;
}

static Std_ReturnType DemSlotWrite(DtcEntryStruct* Dtc)
{
	unsigned int off = DemSlotOffset((unsigned int)Dtc->Slot);
	unsigned int k;
	Std_ReturnType ret = E_OK;

	ret |= DemNvmWrite(off, (uint8)((Dtc->DTC_Code >> 16) & 0xFFU));
	ret |= DemNvmWrite(off + 1U, (uint8)((Dtc->DTC_Code >> 8) & 0xFFU));
	ret |= DemNvmWrite(off + 2U, (uint8)(Dtc->DTC_Code & 0xFFU));
	ret |= DemNvmWrite(off + 3U, Dtc->ErrSts.all);
	ret |= DemNvmWrite(off + 4U, Dtc->ExtData.OccurrenceCnt);
	ret |= DemNvmWrite(off + 5U, (uint8)((Dtc->ExtData.FailCycle >> 8) & 0xFFU));
	ret |= DemNvmWrite(off + 6U, (uint8)(Dtc->ExtData.FailCycle & 0xFFU));

	for (k = 0U; k < DEM_SNAPSHOT_DATA_SIZE; k++)
	{
		ret |= DemNvmWrite(off + DEM_DTC_RECORD_HEADER_SIZE + k,
		                   Dtc->FreezeFrame.Data[k]);
	}

	return ret;
}

/* Erase a slot: only the DTC code has to go, 0xFFFFFF marks it as unused. */
static Std_ReturnType DemSlotErase(unsigned int Slot)
{
	unsigned int off = DemSlotOffset(Slot);
	unsigned int k;
	Std_ReturnType ret = E_OK;

	for (k = 0U; k < 3U; k++)
	{
		ret |= DemNvmWrite(off + k, 0xFFU);
	}

	return ret;
}

/*
 * Format the store: erase every slot and write the header.  Only needed once
 * after a layout change (DEM_STORE_VERSION), and only when the next DTC is
 * stored, so the start-up of a healthy ECU never touches the NVM.
 */
static Std_ReturnType DemStoreFormat(void)
{
	unsigned int slot;
	Std_ReturnType ret = E_OK;

	for (slot = 0U; slot < DEM_NVM_SLOT_NUM; slot++)
	{
		ret |= DemSlotErase(slot);
	}

	if (E_OK == ret)
	{
		ret = DemHeaderWrite();
	}

	return ret;
}

/*
 * Allocate a slot for a new DTC.
 *
 * When the store is full, the oldest DTC (smallest ageing counter) that is not
 * failing at the moment is dropped, so a new fault never fails to find a slot
 * while older, inactive memories exist.  A DTC that is failing right now is
 * never evicted: with more simultaneous faults than slots the surplus DTCs
 * simply stay in RAM (they would otherwise rotate through the store).
 */
static unsigned char DemSlotAlloc(void)
{
	DtcEntryStruct* victim = NULL_PTR;
	unsigned int slot;
	unsigned int i;
	unsigned char best = 0xFFU;

	for (slot = 0U; slot < DEM_NVM_SLOT_NUM; slot++)
	{
		if (0U != DemSlotIsFree(slot))
		{
			return (unsigned char)slot;
		}
	}

	for (i = 0U; i < DtcHandleLen; i++)
	{
		if ((DEM_SLOT_NONE == DtcHandle[i].Slot) ||
		    (0U != DtcHandle[i].ErrSts.bit.testFailed))
		{
			continue;
		}

		if (DtcHandle[i].ExtData.AgingCnt < best)
		{
			best = DtcHandle[i].ExtData.AgingCnt;
			victim = &DtcHandle[i];
		}
	}

	if (NULL_PTR == victim)
	{
		/* Every stored DTC is failing right now: keep the newest fault in RAM
		 * only, the record survives the power cycle as long as it stays in
		 * the store. */
		return DEM_SLOT_NONE;
	}

	slot = (unsigned int)victim->Slot;
	DemDtcClearRuntime(victim);
	victim->Slot = DEM_SLOT_NONE;
	victim->StoreReq = DEM_STORE_IDLE;

	return (unsigned char)slot;
}

/* ------------------------------------------------------------------------- */
/* DTC table helpers                                                         */
/* ------------------------------------------------------------------------- */

static unsigned int DemDtcFindIndex(unsigned int DtcCode)
{
	unsigned int i;

	for (i = 0U; i < DtcHandleLen; i++)
	{
		if ((DtcHandle[i].DTC_Code & DEM_DTC_CODE_UNUSED) ==
		    (DtcCode & DEM_DTC_CODE_UNUSED))
		{
			return i;
		}
	}

	return DEM_INDEX_NONE;
}

static DtcEntryStruct* DemDtcFindMutable(unsigned int DtcCode)
{
	unsigned int index = DemDtcFindIndex(DtcCode);

	return (DEM_INDEX_NONE == index) ? NULL_PTR : &DtcHandle[index];
}

/* Reset everything that describes a DTC, but not its NVM slot (the caller
 * decides whether the slot is kept for an erase or reused). */
static void DemDtcClearRuntime(DtcEntryStruct* Dtc)
{
	Dtc->ErrSts.all = DEM_STATUS_INITIAL;
	Dtc->ExtData.AgingCnt = 0U;
	Dtc->ExtData.OccurrenceCnt = 0U;
	Dtc->ExtData.FailCycle = 0U;
	Dtc->SnapshotValid = 0U;
	memset(&Dtc->FreezeFrame, 0, sizeof(DtcSnapshotData));
}

/* ------------------------------------------------------------------------- */
/* Freeze frame                                                              */
/* ------------------------------------------------------------------------- */

/*
 * Capture the freeze frame of a DTC that is set.
 *
 * DIDs and lengths are configuration (UDS_DemSnapshotDids[]); only the sampled
 * data is kept, one DEM_SNAPSHOT_DID_LEN block per DID, in table order.  A DID
 * that is not configured keeps its zero block.
 */
static void DemSnapshotCapture(DtcSnapshotData* Data)
{
	const DtcSnapshotDidCfg* cfg;
	unsigned int i;
	unsigned int len;

	memset(Data->Data, 0, sizeof(Data->Data));

	for (i = 0U; i < UDS_DemSnapshotDidCount; i++)
	{
		if (i >= DEM_SNAPSHOT_DID_MAX)
		{
			break;
		}

		cfg = &UDS_DemSnapshotDids[i];
		len = (unsigned int)cfg->Len;

		if ((0U == len) || (NULL_PTR == cfg->Read))
		{
			continue;
		}

		if (len > DEM_SNAPSHOT_DID_LEN)
		{
			len = DEM_SNAPSHOT_DID_LEN;
		}

		cfg->Read(&Data->Data[i * DEM_SNAPSHOT_DID_LEN], (unsigned char)len);
	}
}

/* ------------------------------------------------------------------------- */
/* NVM store: execute a requested write / erase                              */
/* ------------------------------------------------------------------------- */

/*
 * Execute the NVM operation that is requested for one DTC.
 *
 * Strict rules (see UDS_DemCfg.h):
 *   - an erase is always allowed (it removes stored fault memory, e.g. 0x14),
 *   - a write is only allowed for a confirmed DTC,
 *   - a failing operation stays requested and is retried by the next request.
 *
 * @return 1 when nothing is pending any more, 0 when it has to be retried.
 */
static unsigned char DemStoreExecute(DtcEntryStruct* Dtc)
{
	if (DEM_STORE_IDLE == Dtc->StoreReq)
	{
		return 1U;
	}

	if (FALSE == Nvm_IsInitialised())
	{
		return 0U;
	}

	if (DEM_STORE_ERASE == Dtc->StoreReq)
	{
		if (DEM_SLOT_NONE != Dtc->Slot)
		{
			if (E_OK != DemSlotErase((unsigned int)Dtc->Slot))
			{
				return 0U;
			}

			Dtc->Slot = DEM_SLOT_NONE;
		}

		Dtc->StoreReq = DEM_STORE_IDLE;
		return 1U;
	}

	/* Write: never store a DTC that is not confirmed. */
	if (0U == Dtc->ErrSts.bit.confirmedDTC)
	{
		Dtc->StoreReq = DEM_STORE_IDLE;
		return 1U;
	}

	if (0U == DemStoreVersionOk())
	{
		/* Fresh device or layout change: format first; the header is written
		 * last, so an interrupted format is simply retried. */
		if (E_OK != DemStoreFormat())
		{
			return 0U;
		}
	}

	if (DEM_SLOT_NONE == Dtc->Slot)
	{
		Dtc->Slot = DemSlotAlloc();
		if (DEM_SLOT_NONE == Dtc->Slot)
		{
			/* Store full and nothing can be dropped. */
			Dtc->StoreReq = DEM_STORE_IDLE;
			return 1U;
		}
	}

	if (E_OK != DemSlotWrite(Dtc))
	{
		return 0U;
	}

	Dtc->StoreReq = DEM_STORE_IDLE;
	return 1U;
}

/* ------------------------------------------------------------------------- */
/* Start-up: restore, age, new operation cycle                               */
/* ------------------------------------------------------------------------- */

static void DemRestore(void)
{
	unsigned int slot;
	unsigned int off;
	unsigned int index;
	unsigned int code;
	unsigned int k;

	if (FALSE == Nvm_IsInitialised())
	{
		return;
	}

	if (0U == DemStoreVersionOk())
	{
		return;
	}

	DemOpCycle = DemCycleRead();

	for (slot = 0U; slot < DEM_NVM_SLOT_NUM; slot++)
	{
		code = DemSlotCodeRead(slot);

		if (DEM_DTC_CODE_UNUSED == code)
		{
			continue;
		}

		index = DemDtcFindIndex(code);
		if (DEM_INDEX_NONE == index)
		{
			/* Left over from an older DTC table: ignored, the slot is reused
			 * the next time a DTC is stored. */
			continue;
		}

		if (DEM_SLOT_NONE != DtcHandle[index].Slot)
		{
			/* The same DTC in two slots (should not happen): keep the first. */
			continue;
		}

		off = DemSlotOffset(slot);

		DtcHandle[index].Slot = (unsigned char)slot;
		/* The whole status byte is restored: testFailed keeps the most recent
		 * test result across the operation cycle (ISO 14229-1, D.2/Figure D.9),
		 * the cycle dependant bits are corrected by DemDtcNewCycle(). */
		DtcHandle[index].ErrSts.all = DemNvmRead(off + 3U);
		DtcHandle[index].ExtData.OccurrenceCnt = DemNvmRead(off + 4U);
		DtcHandle[index].ExtData.FailCycle = (unsigned short)
			(((unsigned short)DemNvmRead(off + 5U) << 8) |
			  (unsigned short)DemNvmRead(off + 6U));

		for (k = 0U; k < DEM_SNAPSHOT_DATA_SIZE; k++)
		{
			DtcHandle[index].FreezeFrame.Data[k] =
				DemNvmRead(off + DEM_DTC_RECORD_HEADER_SIZE + k);
		}

		DtcHandle[index].SnapshotValid = 1U;
		DemRestoredCount++;
	}
}

/* Age the stored DTCs: one that did not fail within DEM_DTC_AGE_MAX operation
 * cycles is deleted. */
static void DemAge(void)
{
	unsigned int i;
	unsigned short age;

	for (i = 0U; i < DtcHandleLen; i++)
	{
		if (DEM_SLOT_NONE == DtcHandle[i].Slot)
		{
			continue;
		}

		age = (unsigned short)(DemOpCycle - DtcHandle[i].ExtData.FailCycle);

		if (age >= DEM_DTC_AGE_MAX)
		{
			/* Aged out: drop the DTC and erase its record. */
			DemDtcClearRuntime(&DtcHandle[i]);
			DtcHandle[i].StoreReq = DEM_STORE_ERASE;
		}
		else
		{
			DtcHandle[i].ExtData.AgingCnt = (unsigned char)(DEM_DTC_AGE_MAX - age);
		}
	}
}

/* Start a new operation cycle for one DTC (ISO 14229-1, D.4 / D.5 / D.9).
 *
 *   testFailedThisOperationCycle        -> 0  (D.4 step 5-7)
 *   testNotCompletedThisOperationCycle  -> 1  (D.9 step 7-9)
 *   pendingDTC -> 0 only when the cycle that just ended completed the test and
 *                 never failed during it (D.5 step 8)
 *
 * testFailed (most recent test result), confirmedDTC, the two "since last
 * clear" bits and warningIndicatorRequested are kept: they are latched until
 * a clear, the ageing or a new test result changes them. */
static void DemDtcNewCycle(DtcEntryStruct* Dtc)
{
	if ((0U == Dtc->ErrSts.bit.testNotCompletedThisOperationCycle) &&
	    (0U == Dtc->ErrSts.bit.testFailedThisOperationCycle))
	{
		Dtc->ErrSts.bit.pendingDTC = 0U;
	}

	Dtc->ErrSts.bit.testFailedThisOperationCycle = 0U;
	Dtc->ErrSts.bit.testNotCompletedThisOperationCycle = 1U;
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

void UDS_DtcInit(DtcEntryStruct* handle, uint16_T Len)
{
	unsigned int i;

	if ((NULL_PTR == handle) || (0U == Len))
	{
		return;
	}

	if (0U != DtcInitDone)
	{
		return;
	}

	DtcHandle = handle;
	DtcHandleLen = (unsigned int)Len;

	DtcSettingOff = 0U;
	DemOpCycle = 0U;
	DemCycleWritePending = 0U;
	DemRestoredCount = 0U;

	for (i = 0U; i < DtcHandleLen; i++)
	{
		DemDtcClearRuntime(&DtcHandle[i]);
		DtcHandle[i].Slot = DEM_SLOT_NONE;
		DtcHandle[i].StoreReq = DEM_STORE_IDLE;
	}

	DemRestore();

	/* New operation cycle for every DTC (restored values first). */
	for (i = 0U; i < DtcHandleLen; i++)
	{
		DemDtcNewCycle(&DtcHandle[i]);
	}

	if (0U != DemRestoredCount)
	{
		/* The operation cycle counter is written once, and only because DTC
		 * memory exists. */
		DemOpCycle++;
		DemCycleWrite();
		DemAge();
	}

	/* Ageing may have decided to delete a record; do it now (nothing is
	 * written when no record changed). */
	for (i = 0U; i < DtcHandleLen; i++)
	{
		(void)DemStoreExecute(&DtcHandle[i]);
	}

	DtcInitDone = 1U;
}

unsigned char UDS_DtcIsInitialized(void)
{
	return DtcInitDone;
}

unsigned char UDS_DtcSetRequest(unsigned int DtcCode)
{
	DtcEntryStruct* dtc = DemDtcFindMutable(DtcCode);

	if ((NULL_PTR == dtc) || (0U == DtcInitDone) || (0U != DtcSettingOff))
	{
		return 1U;
	}

	/* A repeated request of a DTC that is already failed only finishes the
	 * pending NVM operation: the occurrence counter is not counted twice. */
	if (0U != dtc->ErrSts.bit.testFailed)
	{
		return DemStoreExecute(dtc);
	}

	dtc->ErrSts.bit.testFailed = 1U;
	dtc->ErrSts.bit.testFailedThisOperationCycle = 1U;
	dtc->ErrSts.bit.testFailedSinceLastClear = 1U;
	dtc->ErrSts.bit.testNotCompletedSinceLastClear = 0U;
	dtc->ErrSts.bit.testNotCompletedThisOperationCycle = 0U;

#if (DEM_CONFIRM_ON_FIRST_FAIL != 0)
	dtc->ErrSts.bit.confirmedDTC = 1U;
#else
	if (0U != dtc->ErrSts.bit.pendingDTC)
	{
		dtc->ErrSts.bit.confirmedDTC = 1U;
	}
#endif

	dtc->ErrSts.bit.pendingDTC = 1U;

	/* Extended data of this DTC: a new failure restarts its ageing. */
	dtc->ExtData.AgingCnt = DEM_DTC_AGE_MAX;
	dtc->ExtData.FailCycle = DemOpCycle;

	if (dtc->ExtData.OccurrenceCnt < 0xFFU)
	{
		dtc->ExtData.OccurrenceCnt++;
	}

	/* Freeze frame of the first failure; it is kept until the DTC is cleared
	 * or aged out. */
	if (0U == dtc->SnapshotValid)
	{
		DemSnapshotCapture(&dtc->FreezeFrame);
		dtc->SnapshotValid = 1U;
	}

	if ((0U != dtc->WarnIndEnable) && (0U != dtc->ErrSts.bit.confirmedDTC))
	{
		dtc->ErrSts.bit.warningIndicatorRequested = 1U;
	}

	/* The only place a DTC record is written (confirmed DTC only). */
	dtc->StoreReq = DEM_STORE_WRITE;
	return DemStoreExecute(dtc);
}

unsigned char UDS_DtcRecoverRequest(unsigned int DtcCode)
{
	DtcEntryStruct* dtc = DemDtcFindMutable(DtcCode);

	if ((NULL_PTR == dtc) || (0U == DtcInitDone) || (0U != DtcSettingOff))
	{
		return 1U;
	}

	/* Finish an NVM operation that failed earlier. */
	(void)DemStoreExecute(dtc);

	/* Test result "passed" (ISO 14229-1, D.1 / D.8):
	 *   testFailed = 0, the test has run and completed, the warning indicator
	 *   request (which follows the active failure) is withdrawn.
	 * pendingDTC is NOT cleared here: D.5 only clears it when an operation
	 * cycle ends in which the test completed and never failed (see
	 * DemDtcNewCycle()).  The confirmed bit and the DTC memory stay until
	 * 0x14 or the ageing removes them, so this request never writes the NVM. */
	dtc->ErrSts.bit.testFailed = 0U;
	dtc->ErrSts.bit.testNotCompletedSinceLastClear = 0U;
	dtc->ErrSts.bit.testNotCompletedThisOperationCycle = 0U;
	dtc->ErrSts.bit.warningIndicatorRequested = 0U;

	return 1U;
}

void UDS_DtcTestCompletedRequest(unsigned int DtcCode)
{
	DtcEntryStruct* dtc = DemDtcFindMutable(DtcCode);

	if ((NULL_PTR == dtc) || (0U == DtcInitDone))
	{
		return;
	}

	/* The monitor ran and reported a passed result (the fault layer only
	 * reports a matured result, so "no failure" is a pass).  This is what the
	 * monitor sends once per operation cycle, and again after a clear, so the
	 * "not completed" bits and the retained testFailed do not stay stale.
	 * No NVM access: these bits are not part of the DTC memory. */
	dtc->ErrSts.bit.testFailed = 0U;
	dtc->ErrSts.bit.testNotCompletedSinceLastClear = 0U;
	dtc->ErrSts.bit.testNotCompletedThisOperationCycle = 0U;
	dtc->ErrSts.bit.warningIndicatorRequested = 0U;
}

unsigned char UDS_DtcClearGroup(unsigned int Group)
{
	unsigned int i;
	unsigned int code;
	unsigned char matched = 0U;
	unsigned char inGroup;
	unsigned char writeFailed = 0U;

	if (0U == DtcInitDone)
	{
		return DEM_NOT_READY;
	}

	for (i = 0U; i < DtcHandleLen; i++)
	{
		/* Retry an NVM operation that failed earlier. */
		(void)DemStoreExecute(&DtcHandle[i]);

		code = DtcHandle[i].DTC_Code & DEM_DTC_CODE_UNUSED;

		if (DEM_DTC_GROUP_ALL == (Group & DEM_DTC_CODE_UNUSED))
		{
			/* 0xFFFFFF: all groups (ISO 14229-1, D.1). */
			inGroup = 1U;
		}
		else
		{
			/* Exactly this DTC.  Functional groups (0xFFFFxx) and the
			 * manufacturer specific group ranges are not implemented, so they
			 * are answered with NRC 0x31 by the caller. */
			inGroup = (code == Group) ? 1U : 0U;
		}

		if (0U == inGroup)
		{
			continue;
		}

		matched = 1U;

		/* Erase the record right here; only stored DTCs are written. */
		if (DEM_SLOT_NONE != DtcHandle[i].Slot)
		{
			DtcHandle[i].StoreReq = DEM_STORE_ERASE;
			if (0U == DemStoreExecute(&DtcHandle[i]))
			{
				/* The DTC memory could not be written: the caller reports
				 * NRC 0x72 (generalProgrammingFailure), see 12.2.4. */
				writeFailed = 1U;
			}
		}

		DemDtcClearRuntime(&DtcHandle[i]);
	}

	if (0U == matched)
	{
		return DEM_NOT_FOUND;
	}

	return (0U != writeFailed) ? DEM_WRITE_FAILED : DEM_OK;
}

void UDS_DtcSetSettingOff(unsigned char Off)
{
	DtcSettingOff = (0U != Off) ? 1U : 0U;
}

unsigned char UDS_DtcGetSetting(void)
{
	return DtcSettingOff;
}

unsigned char UDS_DtcGetStatusAvailabilityMask(void)
{
	return DEM_STATUS_AVAILABILITY_MASK;
}

const DtcEntryStruct* UDS_DtcGetTable(unsigned int* Count)
{
	if (NULL_PTR != Count)
	{
		*Count = DtcHandleLen;
	}

	return DtcHandle;
}

const DtcEntryStruct* UDS_DtcFind(unsigned int DtcCode)
{
	unsigned int index = DemDtcFindIndex(DtcCode);

	return (DEM_INDEX_NONE == index) ? NULL_PTR : &DtcHandle[index];
}
