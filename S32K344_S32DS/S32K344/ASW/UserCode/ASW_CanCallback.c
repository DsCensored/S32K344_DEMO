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
	{UDS_RESP_ADDR,   &UDS_Resp,   0U, 0U, UDS_CanTpRxCallback, NULL_PTR},
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

/* QCC 04.172 28h: disable only the normal application frames; the UDS CAN-TP
 * has its own transmit path and stays available for diagnostics. */
void ASW_SetNormalCommEnabled(unsigned char enabled)
{
	CAN_CHN1_TX[0].Enable = (0U != enabled) ? 1U : 0U;
}

void ASW_CanInit(void)
{
	for(uint16 i = 0; i < CAN_CHN1_TX_COUNT; i++)
	{
		CAN_CHN1_TX[i].last_send_tick = 0xFFFF0000;
	}
}
