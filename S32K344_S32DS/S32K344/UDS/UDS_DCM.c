/*
 * UDS_DCM.c
 *
 *  Generic DCM core.  Service tables, handlers, routines and security
 *  algorithm are in UDS_DcmCfg.c.
 */

#include "UDS_DCM.h"
#include "UDS_CanTp.h"
#include "UserOS.h"
#include <string.h>

void UDS_DCM_Init(void)
{
	memset(&UDS_DcmCtx, 0, sizeof(UDS_DcmCtx));

	UDS_DcmCtx.Session = UDS_DCM_SESSION_DEFAULT;
	UDS_DcmCtx.SessionTick = BSW_GetSystemTick();

	UDS_DcmCfgInit();
}

static unsigned char UDS_DcmSessionMask(UDS_DcmSessionType session)
{
	switch (session)
	{
		case UDS_DCM_SESSION_PROGRAMMING:
			return UDS_DCM_SESSION_MASK_PROGRAMMING;

		case UDS_DCM_SESSION_EXTENDED:
			return UDS_DCM_SESSION_MASK_EXTENDED;

		case UDS_DCM_SESSION_DEFAULT:
		default:
			return UDS_DCM_SESSION_MASK_DEFAULT;
	}
}

void UDS_DCM_RxIndication(const unsigned char *req, unsigned int len,
                          UDS_DcmAddressType addr)
{
	unsigned char sid;
	unsigned char subFn = UDS_DCM_NO_SUBFN;
	unsigned char suppressPosRsp = 0U;
	unsigned char nrc;
	unsigned char sidFound = 0U;
	unsigned char hasSubFn = 0U;
	unsigned int rspLen = 0U;
	unsigned int i;
	const UDS_DcmServiceType *service = 0;

	UDS_ReqFlg = 0U;

	if ((0 == req) || (0U == len))
	{
		return;
	}

	sid = req[0];

	/* Determine from the configuration table whether this SID has
	 * sub-functions.  This keeps the dispatcher independent of
	 * 0x10/0x11/0x27/0x31 and allows a new service to be added only in
	 * UDS_DcmCfg.c. */
	for (i = 0U; i < UDS_DcmServiceCount; i++)
	{
		if (UDS_DcmServices[i].SID == sid)
		{
			sidFound = 1U;

			if (UDS_DCM_NO_SUBFN != UDS_DcmServices[i].SubFn)
			{
				hasSubFn = 1U;
			}
		}
	}

	if (0U == sidFound)
	{
		UDS_CanSendNRC(sid, NRC_SERVICE_NOT_SUPPORTED);
		return;
	}

	/* NRC 0x11 / 0x12 / 0x7E / 0x7F handling:
	 *
	 *   unknown SID                         -> 0x11
	 *   SID known, sub-function unknown     -> 0x12
	 *   SID/sub-function known but not
	 *     available in the active session   -> 0x7E (sub-function)
	 *                                          0x7F (service without
	 *                                                sub-functions)
	 *
	 * All NRCs are passed to UDS_CanSendNRC().  That function suppresses
	 * them when the request was received on functional addressing.
	 */

	if (0U != hasSubFn)
	{
		if (len < 2U)
		{
			UDS_CanSendNRC(sid, NRC_INCORRECT_MESSAGE_LENGTH_OR_INVALID_FORMAT);
			return;
		}

		subFn = (unsigned char)(req[1] & 0x7FU);
		suppressPosRsp = (unsigned char)((req[1] & 0x80U) ? 1U : 0U);
	}

	/* Look up the exact service/sub-function entry first, ignoring the
	 * active session, so that "unknown sub-function" (0x12) can be told
	 * apart from "not supported in this session" (0x7E/0x7F). */
	for (i = 0U; i < UDS_DcmServiceCount; i++)
	{
		if ((UDS_DcmServices[i].SID == sid) &&
		    (UDS_DcmServices[i].SubFn == subFn))
		{
			service = &UDS_DcmServices[i];
			break;
		}
	}

	if (0 == service)
	{
		UDS_CanSendNRC(sid, NRC_SUBFUNCTION_NOT_SUPPORTED);
		return;
	}

	/* Services that do not support functional addressing (0x31) are
	 * ignored completely: no handler, no positive response.  Their NRCs
	 * would in any case be dropped by UDS_CanSendNRC(). */
	if ((UDS_DCM_ADDRESS_FUNCTIONAL == addr) &&
	    (0U == (service->AddrMask & UDS_DCM_ADDR_MASK_FUNCTIONAL)))
	{
		return;
	}

	if (0U == (service->SessionMask & UDS_DcmSessionMask(UDS_DcmCtx.Session)))
	{
		UDS_CanSendNRC(sid, (0U != hasSubFn) ?
		               NRC_SUBFUNCTION_NOT_SUPPORTED_IN_ACTIVE_SESSION :
		               NRC_SERVICE_NOT_SUPPORTED_IN_ACTIVE_SESSION);
		return;
	}

	if ((UDS_DCM_SECURITY_NONE != service->SecurityLevel) &&
	    ((UDS_DcmCtx.SecurityUnlocked & service->SecurityLevel) !=
	     service->SecurityLevel))
	{
		UDS_CanSendNRC(sid, NRC_SECURITY_ACCESS_DENIED);
		return;
	}

	nrc = service->Handler(req, len, &rspLen);

	if (0U != nrc)
	{
		UDS_CanSendNRC(sid, (UdsNrc_t)nrc);
		return;
	}

	if (0U != suppressPosRsp)
	{
		return;
	}

	UDS_TxBuf[0] = (unsigned char)(sid + 0x40U);

	if (rspLen < 2U)
	{
		rspLen = 2U;
	}

	if (0U == UDS_CanTpSend(rspLen))
	{
		UDS_CanSendNRC(sid, NRC_BUSY_REPEAT_REQUEST);
	}
}

void UDS_DCM_Poll(void)
{
	unsigned int now = BSW_GetSystemTick();

	if ((UDS_DCM_SESSION_DEFAULT != UDS_DcmCtx.Session) &&
	    ((now - UDS_DcmCtx.SessionTick) >= UDS_DCM_SESSION_TIMEOUT_MS))
	{
		UDS_DcmCtx.Session = UDS_DCM_SESSION_DEFAULT;
		UDS_DcmCtx.SecurityUnlocked = 0U;
		UDS_DcmCtx.Seed = 0U;
		UDS_DcmCtx.SeedLevel = 0U;
		UDS_DcmCtx.SecurityFailCount = 0U;
	}

	if ((0U != UDS_DcmCtx.SecurityDelayActive) &&
	    ((now - UDS_DcmCtx.SecurityDelayTick) >= UDS_DCM_SECURITY_DELAY_MS))
	{
		UDS_DcmCtx.SecurityDelayActive = 0U;
		UDS_DcmCtx.SecurityFailCount = 0U;
	}

	if ((0U != UDS_DcmCtx.ResetPending) &&
	    ((now - UDS_DcmCtx.ResetTick) >= UDS_DCM_RESET_DELAY_MS) &&
	    (0U == UDS_CanTpIsTxBusy()))
	{
		UDS_DcmCtx.ResetPending = 0U;
		UDS_DCM_ResetHook();
	}

	UDS_DcmCfgPoll();
}

UDS_DcmSessionType UDS_DCM_GetSession(void)
{
	return UDS_DcmCtx.Session;
}

unsigned char UDS_DCM_IsSecurityUnlocked(void)
{
	return UDS_DcmCtx.SecurityUnlocked;
}


