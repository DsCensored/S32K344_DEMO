/*
 * UDS_DEM.c
 *
 *  Fault diagnosis: monitors, delay and edges.  See UDS_DEM.h.
 *
 *  This module never touches the NVM and owns no DTC data.  It reports the
 *  edges of the debounced fault state to the DTC memory
 *  (UDS_DtcSetRequest / UDS_DtcRecoverRequest) and publishes the error level
 *  of the present faults for the application.
 */

#include "UDS_DEM.h"
#include "UDS_DTC.h"

/* DTC code mask: the low 24 bits are the ISO 14229 DTC. */
#define DEM_DTC_CODE_MASK       (0xFFFFFFUL)

/* Fault detection counter reported when the fault is confirmed. */
#define DEM_FDC_MAX             (127U)

/* Highest error level of all present faults, see UDS_DEM.h. */
uint8_T ASW_stDisChrgErrLevel = 0U;
uint8_T ASW_stChrgErrLevel = 0U;

/* Fault table and runtime state. */
static ErrAppHandleStruct* DemHandle = NULL_PTR;
static unsigned int DemHandleLen = 0U;
static unsigned char DemInitDone = 0U;

static ErrAppHandleStruct* DemFaultFind(unsigned int DtcCode)
{
	unsigned int i;

	for (i = 0U; i < DemHandleLen; i++)
	{
		if ((DemHandle[i].DTC_Code & DEM_DTC_CODE_MASK) ==
		    (DtcCode & DEM_DTC_CODE_MASK))
		{
			return &DemHandle[i];
		}
	}

	return NULL_PTR;
}

void UDS_DemInit(ErrAppHandleStruct* handle, uint16_T Len)
{
	unsigned int i;

	if ((NULL_PTR == handle) || (0U == Len) || (0U != DemInitDone))
	{
		return;
	}

	DemHandle = handle;
	DemHandleLen = (unsigned int)Len;

	for (i = 0U; i < DemHandleLen; i++)
	{
		DemHandle[i].DelayCnt = 0U;
		DemHandle[i].State = 0U;
		DemHandle[i].TestDone = 0U;
		DemHandle[i].ReportedState = 0U;
		DemHandle[i].ReqPending = 0U;
	}

	DemInitDone = 1U;
}

void UDS_DemPolling(ErrAppHandleStruct* handle, uint16_T Len)
{
	unsigned int i;
	unsigned int count;
	unsigned char raw;
	unsigned char newState;
	unsigned char delay;
	unsigned char accepted;
	unsigned char chrg = 0U;
	unsigned char disChrg = 0U;

	if ((0U == DemInitDone) || (NULL_PTR == handle) || (handle != DemHandle))
	{
		return;
	}

	/* The DTC memory has to accept the requests of the first edge: until it is
	 * restored, no fault is evaluated (otherwise the rising edge of a fault
	 * that is present at power-on would be lost). */
	if (0U == UDS_DtcIsInitialized())
	{
		return;
	}

	count = (unsigned int)Len;
	if (count > DemHandleLen)
	{
		count = DemHandleLen;
	}

	for (i = 0U; i < count; i++)
	{
		if (NULL_PTR == handle[i].pFun)
		{
			continue;
		}

		raw = handle[i].pFun();

		/* Delay with hysteresis: the state is only set after the monitor output
		 * has been stable for DelaySteps samples, and only cleared after the
		 * same number of samples in the other direction. */
		delay = handle[i].DelaySteps;

		if (0U != delay)
		{
			if (0U != raw)
			{
				if (handle[i].DelayCnt < delay)
				{
					handle[i].DelayCnt++;
				}
			}
			else if (0U != handle[i].DelayCnt)
			{
				handle[i].DelayCnt--;
			}

			if (0U == handle[i].State)
			{
				newState = (handle[i].DelayCnt >= delay) ? 1U : 0U;
			}
			else
			{
				newState = (0U == handle[i].DelayCnt) ? 0U : 1U;
			}
		}
		else
		{
			/* No delay configured: follow the monitor directly. */
			handle[i].DelayCnt = 0U;
			newState = (0U != raw) ? 1U : 0U;
		}

		handle[i].State = newState;

		/* Application reaction: the error level of a present fault is
		 * published before the DTC request, so a freeze frame captured by the
		 * request already sees the current level. */
		if (0U != newState)
		{
			if (handle[i].VehDisChrgErrLevel > disChrg)
			{
				disChrg = handle[i].VehDisChrgErrLevel;
			}

			if (handle[i].VehChrgErrLevel > chrg)
			{
				chrg = handle[i].VehChrgErrLevel;
			}
		}

		/* The monitor ran at least once, whatever the result is. */
		if (0U == handle[i].TestDone)
		{
			handle[i].TestDone = 1U;
			UDS_DtcTestCompletedRequest(handle[i].DTC_Code);
		}

		/* 0x85 02 freezes the DTC memory but not the diagnosis: the edge stays
		 * pending and is reported once DTC setting is back on. */
		if (0U != UDS_DtcGetSetting())
		{
			continue;
		}

		/* Rising edge: request the DTC to be set.
		 * Falling edge: request the DTC to be recovered. */
		if (handle[i].State != handle[i].ReportedState)
		{
			handle[i].ReportedState = handle[i].State;
			handle[i].ReqPending = 1U;
		}

		/* A request that the DTC memory could not finish (NVM busy) is simply
		 * repeated until it is accepted; the request itself is idempotent. */
		if (0U != handle[i].ReqPending)
		{
			accepted = (0U != handle[i].State) ?
			           UDS_DtcSetRequest(handle[i].DTC_Code) :
			           UDS_DtcRecoverRequest(handle[i].DTC_Code);

			if (0U != accepted)
			{
				handle[i].ReqPending = 0U;
			}
		}
	}

	ASW_stDisChrgErrLevel = disChrg;
	ASW_stChrgErrLevel = chrg;
}

unsigned char UDS_DemIsInitialized(void)
{
	return DemInitDone;
}

void UDS_DemRestartTestReport(void)
{
	unsigned int i;

	for (i = 0U; i < DemHandleLen; i++)
	{
		/* Report the test result again ... */
		DemHandle[i].TestDone = 0U;

		/* ... and re-apply the current fault state, so a fault that is still
		 * present sets its (just cleared) DTC again and a healthy one clears
		 * the "not completed" bits.  The edge is forced by reporting the
		 * opposite of the current state as "already reported". */
		DemHandle[i].ReportedState = (0U == DemHandle[i].State) ? 1U : 0U;
	}
}

unsigned char UDS_DemGetFaultState(unsigned int DtcCode)
{
	const ErrAppHandleStruct* fault = DemFaultFind(DtcCode);

	return (NULL_PTR == fault) ? 0U : fault->State;
}

unsigned char UDS_DemGetFaultDetectionCounter(unsigned int DtcCode)
{
	const ErrAppHandleStruct* fault = DemFaultFind(DtcCode);
	unsigned int scaled;

	if (NULL_PTR == fault)
	{
		return 0U;
	}

	if (0U != fault->State)
	{
		return (unsigned char)DEM_FDC_MAX;
	}

	if (0U == fault->DelaySteps)
	{
		return 0U;
	}

	scaled = ((unsigned int)fault->DelayCnt * DEM_FDC_MAX) /
	         (unsigned int)fault->DelaySteps;

	return (unsigned char)scaled;
}
