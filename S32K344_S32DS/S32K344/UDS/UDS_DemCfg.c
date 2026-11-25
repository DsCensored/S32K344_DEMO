/*
 * UDS_DemCfg.c
 *
 *  DEM project configuration:
 *
 *    ErrHandle[]             the monitored faults (fault diagnosis, UDS_DEM.c)
 *    DtcTable[]              the DTC memory of the ECU (UDS_DTC.c)
 *    UDS_DemSnapshotDids[]   what the freeze frame of a DTC contains
 *    UDS_AppDemInit/Polling  the hooks called by the BSW start-up and the
 *                            10 ms task
 *
 *  Fault table - one row per monitored fault:
 *
 *      VehFaultCode          vehicle specific fault code (application view)
 *      VehDisChrgErrLevel    error level reported while "discharge not allowed"
 *      VehChrgErrLevel       error level reported while "charge not allowed"
 *      pFun                  monitor, returns != 0 while the fault is present
 *      DTC_Code              DTC that this fault sets / recovers
 *      DelaySteps            delay before the fault state changes, 0 = none
 *
 *  DTC table - one row per DTC the ECU supports (this is what 0x19 reports):
 *
 *      DTC_Code              ISO 14229 3-byte DTC
 *      WarnIndEnable         1 = warningIndicatorRequested (status bit 7) used
 *
 *  The fields below are runtime data of UDS_DEM.c / UDS_DTC.c and stay zero in
 *  the tables.
 *
 *  A fault and a DTC are only linked by DTC_Code; a fault whose code is not in
 *  the DTC table is simply not stored (the request is ignored).
 */

#include "UDS_DEM.h"
#include "UDS_DTC.h"
#include "UDS_DemCfg.h"
#include "ASP.h"
#include "UserOS.h"

/* ------------------------------------------------------------------------- */
/* Fault table                                                               */
/* ------------------------------------------------------------------------- */

/*
 * Test hook: write a value != 0 (debugger, test code) to force the example
 * fault and check 0x19 / 0x14 / the freeze frame without a hardware failure.
 */
volatile uint8_T UDS_DemForceExampleFault = 0U;

static uint8_T DEM_ErrExample(void)
{
	return UDS_DemForceExampleFault;
}

static ErrAppHandleStruct ErrHandle[] = {
	/* 示例故障 */
	{
		.VehFaultCode = 0xFFFF,
		.VehDisChrgErrLevel = 3,
		.VehChrgErrLevel = 3,
		.pFun = DEM_ErrExample,
		.DTC_Code = 0x1B0000,
		.DelaySteps = (uint8_T)DEM_FAULT_DELAY_STEPS,
	}
};

/* ------------------------------------------------------------------------- */
/* DTC table                                                                 */
/* ------------------------------------------------------------------------- */

static DtcEntryStruct DtcTable[] = {
	/* 示例故障对应的 DTC */
	{
		.DTC_Code = 0x1B0000,
		.WarnIndEnable = 0U,
	}
};

/* ------------------------------------------------------------------------- */
/* Freeze frame DIDs                                                         */
/* ------------------------------------------------------------------------- */

/* DID values are project specific; keep them in sync with the diagnostic
 * database (and with the DIDs used for 0x22 if the same signal is also a DID). */
#define DEM_DID_KL15        (0x0100U)   /* 1 byte: debounced terminal 15        */
#define DEM_DID_ERR_LEVEL   (0x0101U)   /* 2 bytes: discharge / charge level    */
#define DEM_DID_TIMESTAMP   (0x0102U)   /* 4 bytes: ms since power-up, MSB first*/

static void DemSnapshotReadKL15(unsigned char* Data, unsigned char Len)
{
	if (Len < 1U)
	{
		return;
	}

	Data[0] = ASP_stKL15;
}

static void DemSnapshotReadErrLevel(unsigned char* Data, unsigned char Len)
{
	if (Len < 2U)
	{
		return;
	}

	Data[0] = ASW_stDisChrgErrLevel;
	Data[1] = ASW_stChrgErrLevel;
}

static void DemSnapshotReadTimestamp(unsigned char* Data, unsigned char Len)
{
	unsigned int tick = BSW_GetSystemTick();

	if (Len < 4U)
	{
		return;
	}

	Data[0] = (unsigned char)((tick >> 24) & 0xFFU);
	Data[1] = (unsigned char)((tick >> 16) & 0xFFU);
	Data[2] = (unsigned char)((tick >> 8) & 0xFFU);
	Data[3] = (unsigned char)(tick & 0xFFU);
}

/*
 * Freeze frame DID table: the DTC module samples these DIDs when a DTC is set,
 * and 0x19 04 reports the DID and Len from here.  The data itself is stored
 * without the DID/length.
 */
const DtcSnapshotDidCfg UDS_DemSnapshotDids[] = {
	{DEM_DID_KL15,      1U, DemSnapshotReadKL15},
	{DEM_DID_ERR_LEVEL, 2U, DemSnapshotReadErrLevel},
	{DEM_DID_TIMESTAMP, 4U, DemSnapshotReadTimestamp}
};

const unsigned int UDS_DemSnapshotDidCount =
	(unsigned int)(sizeof(UDS_DemSnapshotDids) / sizeof(UDS_DemSnapshotDids[0]));

/* The DID table must fit into one freeze frame of the store. */
typedef char UDS_DemSnapshotDidCountCheck[
	((sizeof(UDS_DemSnapshotDids) / sizeof(UDS_DemSnapshotDids[0])) <= DEM_SNAPSHOT_DID_MAX)
	? 1 : -1];

/* ------------------------------------------------------------------------- */
/* Hooks                                                                     */
/* ------------------------------------------------------------------------- */

void UDS_AppDemInit(void)
{
	/* The DTC memory first: the fault diagnosis reports its edges to it. */
	UDS_DtcInit(DtcTable, (uint16_T)(sizeof(DtcTable) / sizeof(DtcEntryStruct)));
	UDS_DemInit(ErrHandle, (uint16_T)(sizeof(ErrHandle) / sizeof(ErrAppHandleStruct)));
}

void UDS_AppDemPolling(void)
{
	UDS_DemPolling(ErrHandle, (uint16_T)(sizeof(ErrHandle) / sizeof(ErrAppHandleStruct)));
}
