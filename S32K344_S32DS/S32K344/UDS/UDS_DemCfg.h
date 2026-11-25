/*
 * UDS_DemCfg.h
 *
 *  DEM project configuration and sizing.
 *
 *  The DEM is split in two independent parts:
 *
 *    UDS_DEM.c / UDS_DEM.h   fault diagnosis (诊断故障)
 *        Polls the monitors, applies the fault delay and reports the edges
 *        of the debounced fault state.  It owns no DTC data and never
 *        touches the NVM.
 *
 *    UDS_DTC.c / UDS_DTC.h   DTC memory (维护DTC)
 *        Owns the DTC status byte, ageing, occurrence counter, extended data
 *        and freeze frame of every DTC.  It is strictly request driven (set /
 *        recover / test completed / clear), never polled, and it is the only
 *        part that writes the NVM.
 *
 *  The link between the two is the DTC code: a fault row names the DTC it
 *  requests, and the DTC table (UDS_DemCfg.c) owns the memory of that DTC.
 *
 *  Fault table versus DTC memory
 *  -----------------------------
 *  The fault table may be long (a BMS easily has more than 100 entries).  The
 *  DTC table is the list of supported DTCs reported by 0x19, and the NVM only
 *  holds a pool of DEM_NVM_SLOT_NUM records that confirmed DTCs occupy, so
 *  neither the RAM nor the NVM requirement grows unbounded with the fault
 *  list.
 *
 *  NVM access rules (enforced by UDS_DTC.c):
 *    - one DTC record when a DTC is set (confirmed fault only),
 *    - one slot erase when a DTC is cleared or aged out,
 *    - three header bytes (operation cycle counter) once per power-on, and
 *      only while at least one DTC record exists,
 *    - nothing at all while no fault is stored, and never for a status bit
 *      that the monitors rebuild anyway.
 */

#ifndef UDS_DEMCFG_H_
#define UDS_DEMCFG_H_

#include "rtwtypes.h"
#include "Nvm.h"        /* NVM_BLOCK_DATA_SIZE sizes the store */

/* ------------------------------------------------------------------------- */
/* Timing                                                                    */
/* ------------------------------------------------------------------------- */

/* Period of UDS_AppDemPolling(), i.e. of one fault delay sample. */
#define DEM_TASK_CYCLE_MS             (10U)

/*
 * Fault delay: the debounced fault state only changes after the raw monitor
 * output has been stable for DEM_FAULT_DELAY_STEPS samples.  A fault row may
 * override it with its own DelaySteps value (0 = report the raw result
 * immediately).
 */
#define DEM_FAULT_DELAY_MS            (200U)
#define DEM_FAULT_DELAY_STEPS         (DEM_FAULT_DELAY_MS / DEM_TASK_CYCLE_MS)

#if (DEM_FAULT_DELAY_STEPS < 1)
#error "DEM_FAULT_DELAY_MS must be at least DEM_TASK_CYCLE_MS"
#endif

/* ------------------------------------------------------------------------- */
/* DTC status / ageing                                                       */
/* ------------------------------------------------------------------------- */

/* DTC status bits supported by the ECU (ISO 14229-1, Annex D): all eight. */
#define DEM_STATUS_AVAILABILITY_MASK  (0xFFU)

/*
 * Ageing: number of operation cycles (power-on cycles) without a failure
 * after which a confirmed DTC is deleted from the memory.
 */
#define DEM_DTC_AGE_MAX               (10U)

/*
 * 1: a set request confirms the DTC immediately,
 * 0: confirmation needs a set request in two consecutive operation cycles,
 *    the DTC is only pending after the first one (and nothing is stored until
 *    it is confirmed).
 */
#define DEM_CONFIRM_ON_FIRST_FAIL     (1)

/* Freeze frame record number reported by 0x19 03 / 0x19 04. */
#define DEM_SNAPSHOT_RECORD_NUM       (0x01U)

/* ------------------------------------------------------------------------- */
/* Freeze frame (snapshot) and extended data belong to the DTC               */
/* ------------------------------------------------------------------------- */

/*
 * A freeze frame is a list of data identifiers captured when a DTC is set.
 * 0x19 04 reports it as
 *
 *      59 04 <DTC> <status> <recordNumber> <count> [<DID> <data>]...
 *
 * The DID and its data length are configuration (UDS_DemSnapshotDids[]), they
 * are not stored: the NVM keeps the data of every DID only, one
 * DEM_SNAPSHOT_DID_LEN block per configured DID, in table order.
 */
#define DEM_SNAPSHOT_DID_MAX          (3U)   /* number of DIDs per freeze frame   */
#define DEM_SNAPSHOT_DID_LEN          (4U)   /* max bytes of one DID             */

/* Bytes of freeze frame data that are stored per DTC. */
#define DEM_SNAPSHOT_DATA_SIZE        (DEM_SNAPSHOT_DID_MAX * DEM_SNAPSHOT_DID_LEN)

/* ------------------------------------------------------------------------- */
/* Freeze frame DID table (UDS_DemCfg.c)                                     */
/* ------------------------------------------------------------------------- */

/*
 * One row of the freeze frame DID table: the DID, the number of bytes it
 * contributes and the function that samples it.  The function fills exactly
 * Len bytes, most significant byte first, so the value has the same byte
 * order on the bus as in the 0x22 response of the same signal.
 *
 * The DTC module (UDS_DTC.c) builds the freeze frame from this table when a
 * DTC is set, and the DCM (0x19 04) reports the DIDs and lengths from it.
 */
typedef void (*DtcSnapshotDidReadType)(unsigned char* Data, unsigned char Len);

typedef struct {
	unsigned short Did;
	unsigned char Len;
	DtcSnapshotDidReadType Read;
} DtcSnapshotDidCfg;

extern const DtcSnapshotDidCfg UDS_DemSnapshotDids[];
extern const unsigned int UDS_DemSnapshotDidCount;

/* ------------------------------------------------------------------------- */
/* NVM store (NVM block 2, payload offset, see NvmApp.h / Block2Data)        */
/* ------------------------------------------------------------------------- */

/* Byte offset of the store inside the block payload. */
#define DEM_STORE_OFFSET              (0U)

/* Layout version of the store; see the file comment of UDS_DTC.c. */
#define DEM_STORE_VERSION             (0x02U)

/*
 * Header: version (1 byte) + operation cycle counter (2 bytes, big endian).
 * The counter is incremented once per power-on while DTC records exist; a
 * record stores the cycle it failed in, so the ageing needs no per-DTC write.
 */
#define DEM_STORE_HEADER_SIZE         (3U)

/* DTC (3) + status (1) + occurrences (1) + failure cycle (2) + snapshot data */
#define DEM_DTC_RECORD_HEADER_SIZE    (7U)
#define DEM_DTC_RECORD_SIZE           (DEM_DTC_RECORD_HEADER_SIZE + DEM_SNAPSHOT_DATA_SIZE)

/* Number of DTC records that fit into the store. */
#define DEM_NVM_SLOT_NUM              \
	((NVM_BLOCK_DATA_SIZE - DEM_STORE_HEADER_SIZE) / DEM_DTC_RECORD_SIZE)

/* ------------------------------------------------------------------------- */
/* Application hooks (UDS_DemCfg.c)                                          */
/* ------------------------------------------------------------------------- */

/*
 * Initialise the DEM: the DTC memory (restore from NVM, ageing) and the fault
 * diagnosis (fault table).  Called once from the deferred BSW start-up, after
 * the NVM shadow is available.
 */
void UDS_AppDemInit(void);

/* Run every fault monitor once; called from the 10 ms task.  This does not
 * touch the DTC memory unless a fault state edge has to be reported. */
void UDS_AppDemPolling(void);

#endif /* UDS_DEMCFG_H_ */
