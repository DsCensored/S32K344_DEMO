/*
 * ASW_CanCallback.c
 *
 *  Created on: 2026年10月22日
 *      Author: CENSORED
 */

#include "ASW_CanCallback.h"
#include "AutoSarNm.h"
#include "NMmain.h"
#include "MCAL.h"
#include "UDS_CanTp.h"

CAN_FD_MESSAGE TX_DATA1 =
{
	.ProtocolMode = 0U,
	.Extended = 1U,
	.Length = 8U,
	.Remote = 0U,
	.Error = 0U,
	.BRS = 0U,
	.ESI = 0U,
	.DLC = 8U,
	.ID = 0x1801FF10U,
	.Reserved = 0U,
	.Timestamp = 0.0,
	.Data = {0U}
};


/* Storage for the received NM frame (only the CBV byte is evaluated). */
CAN_FD_MESSAGE CAN_msgAutoSarNmFrameRx = {0U};

CAN_RXStr CAN_CHN1_RX[CAN_CHN1_RX_COUNT] =
{
	{UDS_FUNREQ_ADDR, &UDS_FunReq, 0U, 0U, UDS_CanTpRxCallback, NULL_PTR},
	{UDS_PHYREQ_ADDR, &UDS_PhyReq, 0U, 0U, UDS_CanTpRxCallback, NULL_PTR},
	{CAN_ID_VEH_RX_NM, &CAN_msgAutoSarNmFrameRx, 0U, 0U, NM_CanNmRecvCallBack, NULL_PTR}
};

CAN_TXStr CAN_CHN1_TX[CAN_CHN1_TX_COUNT] =
{
	{
		.Data = &TX_DATA1,
		.send_period = 100U,
		.last_send_tick = 0U,
		.Callback = NULL_PTR,
		.Enable = 1U
	},
	{
		.Data = &CAN_msgAutoSarNmFrame,
		.send_period = CANNM_MSG_CYCLE_TIME,
		.last_send_tick = 0U,
		.Callback = NM_CanNmSendCallBack,
		.Enable = 0U
	}
};

/*
 * 0x28 CommunicationControl.
 *
 * The diagnostic transport (UDS CAN-TP) is never switched off: it has its
 * own transmit path and must stay available so the tester can re-enable
 * communication.  Only the application frames and, for communication type
 * 0x02/0x03, the network management frames are gated here.
 *
 * CommCtrlDisabled is a separate field from Enable so the AutoSarNm state
 * machine can keep controlling Enable without undoing a diagnostic request.
 */

static CAN_TXStr *ASW_FindTxEntry(unsigned int canId)
{
	unsigned int i;

	for (i = 0U; i < CAN_CHN1_TX_COUNT; i++)
	{
		if ((NULL_PTR != CAN_CHN1_TX[i].Data) &&
		    (CAN_CHN1_TX[i].Data->ID == canId))
		{
			return &CAN_CHN1_TX[i];
		}
	}

	return NULL_PTR;
}

static CAN_RXStr *ASW_FindRxEntry(unsigned int canId)
{
	unsigned int i;

	for (i = 0U; i < CAN_CHN1_RX_COUNT; i++)
	{
		if (CAN_CHN1_RX[i].canid == canId)
		{
			return &CAN_CHN1_RX[i];
		}
	}

	return NULL_PTR;
}

static unsigned char ASW_IsDiagnosticRxId(unsigned int canId)
{
	return ((canId == (unsigned int)UDS_FUNREQ_ADDR) ||
	        (canId == (unsigned int)UDS_PHYREQ_ADDR) ||
	        (canId == (unsigned int)UDS_RESP_ADDR)) ? 1U : 0U;
}

void ASW_SetNormalCommEnabled(unsigned char rxEnabled, unsigned char txEnabled)
{
	unsigned int i;

	/* Normal application transmit frames: everything except the NM frame. */
	for (i = 0U; i < CAN_CHN1_TX_COUNT; i++)
	{
		if ((NULL_PTR != CAN_CHN1_TX[i].Data) &&
		    (CAN_CHN1_TX[i].Data->ID != (uint32_T)CAN_ID_VEH_TX_NM))
		{
			CAN_CHN1_TX[i].CommCtrlDisabled = (0U == txEnabled) ? 1U : 0U;
		}
	}

	/* Normal application receive frames: neither diagnostic nor NM. */
	for (i = 0U; i < CAN_CHN1_RX_COUNT; i++)
	{
		if ((0U == ASW_IsDiagnosticRxId(CAN_CHN1_RX[i].canid)) &&
		    (CAN_CHN1_RX[i].canid != (unsigned int)CAN_ID_VEH_RX_NM))
		{
			CAN_CHN1_RX[i].CommCtrlDisabled = (0U == rxEnabled) ? 1U : 0U;
		}
	}
}

void ASW_SetNmCommEnabled(unsigned char rxEnabled, unsigned char txEnabled)
{
	CAN_TXStr *txEntry = ASW_FindTxEntry((unsigned int)CAN_ID_VEH_TX_NM);
	CAN_RXStr *rxEntry = ASW_FindRxEntry((unsigned int)CAN_ID_VEH_RX_NM);

	if (NULL_PTR != txEntry)
	{
		txEntry->CommCtrlDisabled = (0U == txEnabled) ? 1U : 0U;
	}

	if (NULL_PTR != rxEntry)
	{
		rxEntry->CommCtrlDisabled = (0U == rxEnabled) ? 1U : 0U;
	}
}

void ASW_CanInit(void)
{
	for(uint16 i = 0; i < CAN_CHN1_TX_COUNT; i++)
	{
		CAN_CHN1_TX[i].last_send_tick = 0xFFFF0000;
	}
}
