/*
 * UDS_DTC.h
 *
 *  DTC memory (维护DTC).
 *
 *  This part owns the diagnostic trouble code memory: for every DTC of the
 *  table in UDS_DemCfg.c it keeps
 *
 *    - the ISO 14229-1 status byte (Annex D),
 *    - the extended data record  (ageing counter, occurrences, failure cycle),
 *    - the freeze frame / snapshot data (list of DIDs, see UDS_DemCfg.h),
 *    - the NVM slot the DTC occupies.
 *
 *  The extended data and the freeze frame follow the DTC, not the fault: a
 *  fault only requests a DTC operation, the data itself is captured and kept
 *  by this module.
 *
 *  Everything here is request driven - there is no polling function:
 *
 *      UDS_DtcSetRequest()            rising edge of a fault  -> set/confirm
 *      UDS_DtcRecoverRequest()        falling edge of a fault -> recover
 *      UDS_DtcTestCompletedRequest()  the monitor ran at least once
 *      UDS_DtcClearGroup()            0x14 ClearDiagnosticInformation
 *      UDS_DtcInit()                  start-up: restore, age, new cycle
 *
 *  Only the operations of this module write the NVM (see the rules in
 *  UDS_DemCfg.h): a set request that is not confirmed, a recover request and
 *  a "test completed" request never do.
 */

#ifndef _UDS_DTC
#define _UDS_DTC

#include "rtwtypes.h"
#include "Std_Types.h"
#include "UDS_DemCfg.h"

/* Return values of the DTC API (0 = success). */
#define DEM_OK          (0U)
#define DEM_NOT_FOUND   (1U)   /* DTC / DTC group is not configured      */
#define DEM_NOT_READY   (2U)   /* DTC memory not initialised yet         */
#define DEM_WRITE_FAILED (3U)  /* the DTC memory could not be written    */

/* groupOfDTC value that addresses all DTC groups (0x14). */
#define DEM_DTC_GROUP_ALL           (0xFFFFFFUL)

/* NVM slot of a DTC that is not stored (yet). */
#define DEM_SLOT_NONE               (0xFFU)

/* Values of DtcEntryStruct.StoreReq (pending NVM operation). */
#define DEM_STORE_IDLE              (0U)
#define DEM_STORE_WRITE             (1U)
#define DEM_STORE_ERASE             (2U)

/*
 * ISO 14229-1 statusOfDTC byte.
 *
 * The bit fields are declared in bus order: GCC assigns them starting at bit 0
 * of the container, so `all` (the lowest byte of the little endian word)
 * carries testFailed in bit 0 ... warningIndicatorRequested in bit 7, which is
 * exactly the byte that is sent on the bus.
 */
typedef union {
	unsigned char all;
	struct {
		unsigned testFailed							: 1;
		unsigned testFailedThisOperationCycle		: 1;
		unsigned pendingDTC							: 1;
		unsigned confirmedDTC						: 1;
		unsigned testNotCompletedSinceLastClear		: 1;
		unsigned testFailedSinceLastClear			: 1;
		unsigned testNotCompletedThisOperationCycle : 1;
		unsigned warningIndicatorRequested			: 1;
	}bit;
}DTC_Sts;

/*
 * Extended data of one DTC (0x19 06).
 *
 * It follows the DTC: the ageing counter counts the operation cycles without
 * a failure, the occurrence counter the set requests since the last clear.
 * FailCycle is the operation cycle of the last failure and lets the ageing
 * be derived without rewriting the record every power-on.
 */
typedef struct {
	unsigned char AgingCnt;        /* DEM_DTC_AGE_MAX = current failure      */
	unsigned char OccurrenceCnt;   /* failures since the last clear          */
	unsigned short FailCycle;      /* operation cycle of the last failure    */
} DtcExtData;

/*
 * Freeze frame data of one DTC: the sampled data of the configured DIDs, one
 * DEM_SNAPSHOT_DID_LEN block per DID, in the order of UDS_DemSnapshotDids[].
 */
typedef struct {
	unsigned char Data[DEM_SNAPSHOT_DATA_SIZE];
} DtcSnapshotData;

/*
 * One DTC of the memory.
 *
 * The first fields are the configuration (UDS_DemCfg.c), everything below
 * ErrSts is runtime/persistent data maintained by this module.
 */
typedef struct {
	/* --- configuration (UDS_DemCfg.c) ------------------------------------ */
	unsigned int DTC_Code;            /* ISO 14229 3-byte DTC, low 24 bit used */
	unsigned char WarnIndEnable;      /* 1 = supports warningIndicatorRequested*/

	/* --- runtime / persistent data (UDS_DTC.c) --------------------------- */
	DTC_Sts ErrSts;                   /* status byte                           */
	DtcExtData ExtData;               /* extended data record                  */
	DtcSnapshotData FreezeFrame;      /* freeze frame data                     */
	unsigned char SnapshotValid;      /* 1 = FreezeFrame holds a captured one  */
	unsigned char Slot;               /* NVM slot, DEM_SLOT_NONE = not stored  */
	unsigned char StoreReq;           /* DEM_STORE_IDLE/WRITE/ERASE            */
} DtcEntryStruct;

/* ------------------------------------------------------------------------- */
/* Life cycle                                                                */
/* ------------------------------------------------------------------------- */

/*
 * Initialise the DTC memory for one DTC table and restore it from the NVM.
 *
 * Must be called after the NVM shadow is available (deferred BSW start-up,
 * UDS_AppDemInit()).  Restores the confirmed DTCs, ages the DTCs of the cycle
 * that just ended and opens a new operation cycle.
 *
 * @param handle DTC table (must stay valid, it holds the runtime data).
 * @param Len    Number of DTCs in the table (any length).
 */
void UDS_DtcInit(DtcEntryStruct* handle, uint16_T Len);

/* TRUE once UDS_DtcInit() completed. */
unsigned char UDS_DtcIsInitialized(void);

/* ------------------------------------------------------------------------- */
/* Requests (the only way the DTC memory changes)                            */
/* ------------------------------------------------------------------------- */

/*
 * Set request: the fault of this DTC is present (rising edge of the debounced
 * fault state).
 *
 * Updates the status byte (testFailed, testFailedThisOperationCycle,
 * testFailedSinceLastClear, pendingDTC, confirmedDTC, warning indicator),
 * increments the occurrence counter, restarts the ageing, captures the freeze
 * frame on the first failure and writes the NVM record of a confirmed DTC.
 *
 * The request is ignored while DTC setting is off (0x85 02) and for a DTC that
 * is not configured.
 */
unsigned char UDS_DtcSetRequest(unsigned int DtcCode);

/*
 * Recover request: the fault of this DTC is gone (falling edge of the
 * debounced fault state).
 *
 * Test result "passed" (ISO 14229-1, D.1 / D.8): testFailed is cleared and the
 * warning indicator request is withdrawn.  pendingDTC stays set until an
 * operation cycle has completed in which the test passed and never failed
 * (Table D.5), see UDS_DtcInit().  The confirmed bit and the memory stay until
 * 0x14 or the ageing removes them, so this request never writes the NVM.
 */
unsigned char UDS_DtcRecoverRequest(unsigned int DtcCode);

/*
 * The monitor of this DTC ran and reported a passed result: testFailed is
 * cleared, both "test not completed" bits are cleared and the warning
 * indicator request is withdrawn.  The fault layer sends this once per
 * operation cycle and again after a clear.  Never writes the NVM.
 */
void UDS_DtcTestCompletedRequest(unsigned int DtcCode);

/* ------------------------------------------------------------------------- */
/* 0x14 ClearDiagnosticInformation / 0x85 ControlDtcSetting                  */
/* ------------------------------------------------------------------------- */

/*
 * Clear the DTCs of one group of DTC.
 *
 *   DEM_DTC_GROUP_ALL (0xFFFFFF)  every DTC (ISO 14229-1, D.1)
 *   anything else                 exactly this DTC code
 *
 * Functional groups (0xFFFFxx) and the manufacturer specific group ranges are
 * not implemented; the caller answers NRC 0x31 for them.
 *
 * The status byte, extended data, freeze frame and the NVM record are reset
 * (ISO 14229-1, 12.2.1).  Only records that are actually stored are written
 * (erased).  DTC setting off does not prevent a clear (10.8.1).
 *
 * @return DEM_OK, DEM_NOT_FOUND when the group is not supported, DEM_NOT_READY
 *         when the DTC memory is not initialised, DEM_WRITE_FAILED when a
 *         record could not be erased (the caller answers NRC 0x72).
 */
unsigned char UDS_DtcClearGroup(unsigned int Group);

/*
 * 0x85 ControlDtcSetting: 1 freezes the DTC memory (set and recover requests
 * are ignored, nothing is stored), 0 resumes normal operation.
 * The DCM switches DTC setting back on when the default session is entered.
 */
void UDS_DtcSetSettingOff(unsigned char Off);
unsigned char UDS_DtcGetSetting(void);

/* ------------------------------------------------------------------------- */
/* Data providers for 0x19 ReadDTCInformation                                */
/* ------------------------------------------------------------------------- */

/* DTC status bits that this ECU supports (0x19 01/02/0A/14 response byte 1).*/
unsigned char UDS_DtcGetStatusAvailabilityMask(void);

/*
 * Read access to the DTC table.  The pointer is valid until the next
 * UDS_DtcInit(); the runtime fields are the live DTC state.
 *
 * @param Count Destination of the number of DTCs (may be NULL).
 * @return Pointer to the table, NULL_PTR before UDS_DtcInit().
 */
const DtcEntryStruct* UDS_DtcGetTable(unsigned int* Count);

/* Find one configured DTC by its ISO 14229 3-byte code, NULL_PTR if unknown. */
const DtcEntryStruct* UDS_DtcFind(unsigned int DtcCode);

#endif /* _UDS_DTC */
