/*
 * UDS_DEM.h
 *
 *  Fault diagnosis (诊断故障).
 *
 *  This part only diagnoses faults:
 *
 *    - every fault row of UDS_DemCfg.c has a monitor (pFun) that returns
 *      != 0 while the fault is present,
 *    - the raw result is debounced with a delay (DelaySteps samples of
 *      DEM_TASK_CYCLE_MS): the debounced fault state only changes after the
 *      monitor output has been stable for that long,
 *    - a rising edge of the debounced state requests the DTC to be set, a
 *      falling edge requests it to be recovered
 *      (UDS_DtcSetRequest / UDS_DtcRecoverRequest),
 *    - the highest error level of all present faults is published for the
 *      application (ASW_stDisChrgErrLevel / ASW_stChrgErrLevel).
 *
 *  This module owns no DTC data and never touches the NVM: DTC status bytes,
 *  ageing, extended data and the freeze frame belong to UDS_DTC.c, which only
 *  acts when it is requested.
 *
 *  The fault table lives in UDS_DemCfg.c and may be long (a BMS easily has
 *  more than 100 rows).  A row names the DTC code it requests; the DTC code
 *  is the only link to the DTC memory (UDS_DemCfg.c owns the DTC table).
 */

#ifndef _UDS_DEM
#define _UDS_DEM

#include "rtwtypes.h"
#include "Std_Types.h"
#include "UDS_DemCfg.h"

#define UDS_DEM_ENABLE (0U)

/* Type of a fault monitor: returns != 0 while the fault is present. */
typedef unsigned char(*FaultAlarmFunc)(void);

/*
 * One monitored fault, i.e. one entry of the fault table in UDS_DemCfg.c.
 *
 * The first block is the static configuration, the second block is runtime
 * data that only this module touches.
 */
typedef struct {
	/* --- configuration (UDS_DemCfg.c) ------------------------------------ */
	unsigned short VehFaultCode;      /* vehicle specific fault code          */
	unsigned char VehDisChrgErrLevel; /* error level "discharge not allowed"  */
	unsigned char VehChrgErrLevel;    /* error level "charge not allowed"     */
	FaultAlarmFunc pFun;              /* monitor, != 0 while the fault is on  */
	unsigned int DTC_Code;            /* DTC requested by this fault          */
	unsigned char DelaySteps;         /* delay, 0 = report the raw result     */

	/* --- runtime data (UDS_DEM.c) ---------------------------------------- */
	unsigned char DelayCnt;           /* 0..DelaySteps                        */
	unsigned char State;              /* diagnosed fault state, 0/1           */
	unsigned char ReportedState;      /* state last handed to the DTC memory  */
	unsigned char TestDone;           /* 1 = "test completed" was reported    */
	unsigned char ReqPending;         /* 1 = the DTC request was not accepted */
} ErrAppHandleStruct;

/* ------------------------------------------------------------------------- */
/* Life cycle                                                                */
/* ------------------------------------------------------------------------- */

/*
 * Initialise the fault diagnosis for one fault table.
 *
 * No NVM access: the DTC memory is restored by UDS_DtcInit().  The fault
 * polling stays inactive until the DTC memory is ready, so a fault that is
 * already present when the ECU starts is reported as a normal rising edge
 * once the DTC memory can accept it.
 *
 * @param handle Fault table (must stay valid, it holds the runtime data).
 * @param Len    Number of rows in the table (any length).
 */
void UDS_DemInit(ErrAppHandleStruct* handle, uint16_T Len);

/*
 * Run all fault monitors once: update the delay counters, report fault state
 * edges to the DTC memory and publish the error levels of the application.
 * Call periodically every DEM_TASK_CYCLE_MS.
 *
 * A fault state edge calls UDS_DtcSetRequest() / UDS_DtcRecoverRequest(); that
 * is the only moment this function reaches into the DTC memory, and only when
 * an edge really happened.
 */
void UDS_DemPolling(ErrAppHandleStruct* handle, uint16_T Len);

/* TRUE once UDS_DemInit() completed. */
unsigned char UDS_DemIsInitialized(void);

/*
 * Re-apply the current fault state and report the test result again on the
 * next poll.
 *
 * Called after a successful ClearDiagnosticInformation: the clear resets the
 * DTC memory (also the two "not completed" status bits, ISO 14229-1, Table
 * D.7 / D.9), so a fault that is still present has to set its DTC again and a
 * healthy one has to report its passed result once.
 */
void UDS_DemRestartTestReport(void);

/* ------------------------------------------------------------------------- */
/* Queries (0x19 14 and application diagnostics)                             */
/* ------------------------------------------------------------------------- */

/* Debounced fault state of the fault that requests DtcCode (0 = no fault). */
unsigned char UDS_DemGetFaultState(unsigned int DtcCode);

/*
 * ISO 14229-1 fault detection counter of DtcCode, scaled from the delay
 * counter: 0 = no fault detected, 127 = fault confirmed.  Returns 0 when no
 * fault requests this DTC.
 */
unsigned char UDS_DemGetFaultDetectionCounter(unsigned int DtcCode);

/* ------------------------------------------------------------------------- */
/* Application view                                                          */
/* ------------------------------------------------------------------------- */

/*
 * Highest error level of all present faults, used by the application (charge /
 * discharge inhibit).  Written by UDS_DemPolling().
 */
extern uint8_T ASW_stDisChrgErrLevel;
extern uint8_T ASW_stChrgErrLevel;

#endif /* _UDS_DEM */
