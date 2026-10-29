/*
 * UDS_CanTp.c
 *
 *  Created on: 2026年10月23日
 *      Author: CENSORED
 */

#include "UDS_CanTp.h"
#include "UDS_DCM.h"
#include "Can_43_FLEXCAN.h"
#include "UserOS.h"
#include <string.h>

CAN_FD_MESSAGE UDS_FunReq, UDS_PhyReq, UDS_Resp;

unsigned char UDS_ReqFlg = 0;
unsigned char UDS_RxBuf[UDS_RX_BUF_LEN];
unsigned char UDS_TxBuf[UDS_TX_BUF_LEN];

UDS_TpStr UDS_RxReq;

#define UDS_TP_HTH              1U
#define UDS_TP_CAN_EXTENDED     0x80000000UL

#define UDS_TP_TX_IDLE          0U
#define UDS_TP_TX_SF            1U
#define UDS_TP_TX_FF            2U
#define UDS_TP_TX_WAIT_FC       3U
#define UDS_TP_TX_CF            4U

/* Receive side timing/state. */
static unsigned char UDS_RxActive = 0U;
static unsigned int UDS_RxLen = 0U;
static unsigned int UDS_RxOffset = 0U;
static unsigned char UDS_RxSN = 0U;
static unsigned int UDS_RxLastTick = 0U;

/* Transmit side uses a private shadow, so UDS_TxBuf may be reused by the
 * application while a segmented response is still being transmitted. */
static unsigned char UDS_TxShadow[UDS_TX_BUF_LEN];
static unsigned int UDS_TxLen = 0U;
static unsigned int UDS_TxOffset = 0U;
static unsigned char UDS_TxSN = 0U;
static unsigned char UDS_TxSTmin = 0U;
static unsigned char UDS_TxState = UDS_TP_TX_IDLE;
static unsigned char UDS_TxBusy = 0U;
static unsigned int UDS_TxLastTick = 0U;

/* FC/NRC have priority over the normal response queue. */
static unsigned char UDS_CtrlBuf[8];
static unsigned char UDS_CtrlLen = 0U;
static unsigned char UDS_CtrlPending = 0U;
static unsigned int UDS_CtrlLastTick = 0U;
static Can_IdType UDS_CtrlId = UDS_RESP_ADDR;

static void UDS_CanTpQueueCtrl(Can_IdType canId, const unsigned char *buf, unsigned char len)
{
	memcpy(UDS_CtrlBuf, buf, len);
	(void)canId;
	UDS_CtrlLen = len;
	UDS_CtrlId = UDS_RESP_ADDR;
	UDS_CtrlPending = 1U;
	UDS_CtrlLastTick = BSW_GetSystemTick();
}

void UDS_CanSendNRCForAddr(unsigned char SID, UdsNrc_t NRC, unsigned char addr)
{
	unsigned char buf[8];

	/* ISO 14229-1: a negative response shall not be sent for a request
	 * received on functional addressing.  All NRC paths (unknown service,
	 * unknown sub-function, wrong session, security, handler NRC, ...)
	 * end up here, so this is the single place where they are filtered.
	 *
	 * The addressing mode is a parameter because the response does not
	 * always belong to the most recent request: while a handler answers
	 * with NRC 0x78, a new request may update UDS_RxReq.Addr. */
	if (UDS_DCM_ADDRESS_FUNCTIONAL == addr)
	{
		switch(NRC)
		{
		case NRC_SERVICE_NOT_SUPPORTED:
		case NRC_SUBFUNCTION_NOT_SUPPORTED:
		case NRC_REQUEST_OUT_OF_RANGE:
		case NRC_SUBFUNCTION_NOT_SUPPORTED_IN_ACTIVE_SESSION:
		case NRC_SERVICE_NOT_SUPPORTED_IN_ACTIVE_SESSION:
			return;
		default:
			break;
		}
	}

	buf[0] = 0x03U;
	buf[1] = 0x7FU;
	buf[2] = SID;
	buf[3] = (unsigned char)NRC;
	buf[4] = UDS_INVFIL;
	buf[5] = UDS_INVFIL;
	buf[6] = UDS_INVFIL;
	buf[7] = UDS_INVFIL;

	UDS_CanTpQueueCtrl(UDS_RESP_ADDR, buf, 8U);
}

void UDS_CanSendNRC(unsigned char SID, UdsNrc_t NRC)
{
	UDS_CanSendNRCForAddr(SID, NRC, UDS_RxReq.Addr);
}

/*
 * The flow-control frame uses the same CAN ID as the segmented message.
 * When the ECU receives a multi-frame request, that ID is the request ID.
 */
static void UDS_CanSendFC(void)
{
	unsigned char buf[8];

	buf[0] = 0x30U;
	buf[1] = 0x00U;
	buf[2] = UDS_N_CS;
	buf[3] = 0x00U;
	buf[4] = 0x00U;
	buf[5] = 0x00U;
	buf[6] = 0x00U;
	buf[7] = 0x00U;

	UDS_CanTpQueueCtrl(UDS_RESP_ADDR, buf, 8U);
}

void UDS_CanTpInit(void)
{
	memset(UDS_RxBuf, 0, UDS_RX_BUF_LEN);
	memset(UDS_TxBuf, 0, UDS_TX_BUF_LEN);
	memset(UDS_TxShadow, 0, UDS_TX_BUF_LEN);

	UDS_ReqFlg = 0U;
	UDS_RxReq.SID = 0U;
	UDS_RxReq.Len = 0U;
	UDS_RxReq.Buf = UDS_RxBuf;
	UDS_RxReq.Addr = (unsigned char)UDS_DCM_ADDRESS_PHYSICAL;

	UDS_RxActive = 0U;
	UDS_RxLen = 0U;
	UDS_RxOffset = 0U;
	UDS_RxSN = 0U;
	UDS_RxLastTick = 0U;

	UDS_TxLen = 0U;
	UDS_TxOffset = 0U;
	UDS_TxSN = 0U;
	UDS_TxSTmin = 0U;
	UDS_TxState = UDS_TP_TX_IDLE;
	UDS_TxBusy = 0U;
	UDS_TxLastTick = 0U;

	UDS_CtrlLen = 0U;
	UDS_CtrlPending = 0U;
	UDS_CtrlLastTick = 0U;
}

static Std_ReturnType UDS_CanTpWrite(Can_IdType canId, const unsigned char *data, unsigned char len)
{
	Can_PduType pdu;

	pdu.id = (Can_IdType)(canId | UDS_TP_CAN_EXTENDED);
	pdu.swPduHandle = 0U;
	pdu.length = len;
	pdu.sdu = (uint8 *)data;

	return Can_43_FLEXCAN_Write((Can_HwHandleType)UDS_TP_HTH, &pdu);
}

unsigned char UDS_CanTpIsTxBusy(void)
{
	return UDS_TxBusy;
}

void UDS_CanTpAbort(void)
{
	UDS_TxState = UDS_TP_TX_IDLE;
	UDS_TxBusy = 0U;
	UDS_TxLen = 0U;
	UDS_TxOffset = 0U;
	UDS_TxSN = 0U;
	UDS_TxSTmin = 0U;
	UDS_CtrlPending = 0U;
	UDS_CtrlLen = 0U;
}

unsigned char UDS_CanTpSend(unsigned int len)
{
	if ((0U == len) || (len > UDS_TX_BUF_LEN) || (0U != UDS_TxBusy))
	{
		return 0U;
	}

	/* Copy now; the application may overwrite UDS_TxBuf immediately. */
	memcpy(UDS_TxShadow, UDS_TxBuf, len);

	UDS_TxLen = len;
	UDS_TxOffset = 0U;
	UDS_TxSN = 1U;
	UDS_TxSTmin = 0U;
	UDS_TxBusy = 1U;
	UDS_TxLastTick = BSW_GetSystemTick();
	UDS_TxState = (len <= 7U) ? UDS_TP_TX_SF : UDS_TP_TX_FF;

	return 1U;
}

static void UDS_CanTpRxFlowControl(CAN_FD_MESSAGE *Data)
{
	unsigned int now = BSW_GetSystemTick();
	unsigned char status;
	unsigned char stmin;

	if (UDS_TP_TX_WAIT_FC != UDS_TxState)
	{
		return;
	}

	status = (unsigned char)(Data->Data[0] & 0x0FU);

	if (0U == status)
	{
		stmin = Data->Data[2];
		/* 0xF1..0xF9 are sub-millisecond values; the 5 ms poll cannot
		 * represent them, so use 1 ms as the smallest practical value. */
		if (stmin > 0x7FU)
		{
			stmin = 1U;
		}
		UDS_TxSTmin = stmin;
		UDS_TxState = UDS_TP_TX_CF;
		UDS_TxLastTick = now;
	}
	else if (1U == status)
	{
		/* WAIT: restart N_Bs. */
		UDS_TxLastTick = now;
	}
	else
	{
		/* Overflow/abort. */
		UDS_CanTpAbort();
	}
}

void UDS_CanTpRxCallback(CAN_FD_MESSAGE *Data)
{
	unsigned int now = BSW_GetSystemTick();
	unsigned int remaining;
	unsigned int copyLen;
	unsigned char length;
	UDS_FrameType frameType;
	UDS_DcmAddressType addr = UDS_DCM_ADDRESS_PHYSICAL;

	if ((0U == Data->Length) || (NULL_PTR == Data->Data))
	{
		return;
	}

	frameType = (UDS_FrameType)(Data->Data[0] & (unsigned char)kMask);

	/* Flow control for a response we are transmitting. */
//	if (UDS_PHYREQ_ADDR == Data->ID)
//	{
		if (kFC == frameType)
		{
			UDS_CanTpRxFlowControl(Data);
			return;
		}
//	}

	if (UDS_FUNREQ_ADDR == Data->ID)
	{
		/* Functional addressing only supports single-frame requests. */
		if (kSF != frameType)
		{
			return;
		}

		addr = UDS_DCM_ADDRESS_FUNCTIONAL;
	}
	else if (UDS_PHYREQ_ADDR != Data->ID)
	{
		return;
	}

	/* Remember the addressing mode for UDS_CanSendNRC(), which suppresses
	 * negative responses for functionally addressed requests. */
	UDS_RxReq.Addr = (unsigned char)addr;

	if (kSF == frameType)
	{
		length = (unsigned char)(Data->Data[0] & 0x0FU);
		if ((0U == length) || (length > 7U))
		{
			return;
		}

		memcpy(UDS_RxBuf, &Data->Data[1], length);
		UDS_RxReq.Len = length;
		UDS_RxReq.SID = UDS_RxBuf[0];
		UDS_RxReq.Buf = UDS_RxBuf;
		UDS_ReqFlg = 1U;
		UDS_RxActive = 0U;
		UDS_DCM_RxIndication(UDS_RxBuf, UDS_RxReq.Len, addr);
	}
	else if (kFF == frameType)
	{
		/* 12-bit length of the first frame: (Data[0] & 0x0F) << 8 | Data[1].
		 * Keep the shift in a 32-bit expression - truncating it to 8 bits
		 * drops the high nibble for every request longer than 255 bytes. */
		UDS_RxLen = (((unsigned int)(Data->Data[0] & 0x0FU)) << 8) |
		            (unsigned int)Data->Data[1];

		if ((UDS_RxLen < 8U) || (UDS_RxLen > UDS_RX_BUF_LEN))
		{
			UDS_CanSendNRC(Data->Data[2], NRC_INCORRECT_MESSAGE_LENGTH_OR_INVALID_FORMAT);
			return;
		}

		/* First frame carries six bytes: SID + five data bytes. */
		memcpy(UDS_RxBuf, &Data->Data[2], 6U);
		UDS_RxOffset = 6U;
		UDS_RxSN = 1U;
		UDS_RxActive = 1U;
		UDS_RxLastTick = now;
		UDS_RxReq.Len = UDS_RxLen;
		UDS_RxReq.SID = UDS_RxBuf[0];
		UDS_RxReq.Buf = UDS_RxBuf;

		/* Ask the sender to continue on the request ID. */
		UDS_CanSendFC();
	}
	else if (kCF == frameType)
	{
		if (0U == UDS_RxActive)
		{
			return;
		}

		if ((Data->Data[0] & 0x0FU) != UDS_RxSN)
		{
			/* Sequence error: drop the incomplete request. */
			UDS_RxActive = 0U;
			UDS_RxOffset = 0U;
			UDS_RxSN = 0U;
			return;
		}

		remaining = UDS_RxReq.Len - UDS_RxOffset;
		copyLen = (remaining > 7U) ? 7U : remaining;

		memcpy(&UDS_RxBuf[UDS_RxOffset], &Data->Data[1], copyLen);
		UDS_RxOffset += copyLen;
		UDS_RxLastTick = now;

		if (UDS_RxOffset >= UDS_RxReq.Len)
		{
			UDS_RxActive = 0U;
			UDS_ReqFlg = 1U;
			UDS_DCM_RxIndication(UDS_RxBuf, UDS_RxReq.Len, addr);
		}
		else
		{
			UDS_RxSN = (unsigned char)((UDS_RxSN + 1U) & 0x0FU);
		}
	}
}

void UDS_CanTpTxPoll(void)
{
	unsigned int now = BSW_GetSystemTick();
	unsigned int remaining;
	unsigned int copyLen;
	unsigned char frame[8];

	if (0U != UDS_CtrlPending)
	{
		/* FC/NRC use a separate control buffer and are sent first. */
		if (E_OK == UDS_CanTpWrite(UDS_CtrlId, UDS_CtrlBuf, UDS_CtrlLen))
		{
			UDS_CtrlPending = 0U;
			UDS_CtrlLen = 0U;
		}
		return;
	}

	if (0U == UDS_TxBusy)
	{
		return;
	}

	memset(frame, 0, sizeof(frame));

	if (UDS_TP_TX_SF == UDS_TxState)
	{
		frame[0] = (unsigned char)UDS_TxLen;
		memcpy(&frame[1], UDS_TxShadow, UDS_TxLen);

		if (E_OK == UDS_CanTpWrite(UDS_RESP_ADDR, frame, 8U))
		{
			UDS_TxBusy = 0U;
			UDS_TxState = UDS_TP_TX_IDLE;
		}
	}
	else if (UDS_TP_TX_FF == UDS_TxState)
	{
		frame[0] = (unsigned char)(0x10U | ((UDS_TxLen >> 8) & 0x0FU));
		frame[1] = (unsigned char)(UDS_TxLen & 0xFFU);
		memcpy(&frame[2], UDS_TxShadow, 6U);

		if (E_OK == UDS_CanTpWrite(UDS_RESP_ADDR, frame, 8U))
		{
			UDS_TxOffset = 6U;
			UDS_TxState = UDS_TP_TX_WAIT_FC;
			UDS_TxLastTick = now;
		}
	}
	else if (UDS_TP_TX_WAIT_FC == UDS_TxState)
	{
		/* Wait for the tester's flow-control frame. */
		return;
	}
	else if (UDS_TP_TX_CF == UDS_TxState)
	{
		if ((now - UDS_TxLastTick) < UDS_TxSTmin)
		{
			return;
		}

		remaining = UDS_TxLen - UDS_TxOffset;
		copyLen = (remaining > 7U) ? 7U : remaining;

		frame[0] = (unsigned char)(0x20U | (UDS_TxSN & 0x0FU));
		memcpy(&frame[1], &UDS_TxShadow[UDS_TxOffset], copyLen);

		if (E_OK == UDS_CanTpWrite(UDS_RESP_ADDR, frame, 8U))
		{
			UDS_TxOffset += copyLen;
			UDS_TxSN = (unsigned char)((UDS_TxSN + 1U) & 0x0FU);
			UDS_TxLastTick = now;

			if (UDS_TxOffset >= UDS_TxLen)
			{
				UDS_TxBusy = 0U;
				UDS_TxState = UDS_TP_TX_IDLE;
			}
		}
	}
}

void UDS_CanTpTimerPoll(void)
{
	unsigned int now = BSW_GetSystemTick();

	/* N_Cr: receiver waits for the next consecutive frame. */
	if ((0U != UDS_RxActive) && ((now - UDS_RxLastTick) >= UDS_N_CR))
	{
		UDS_RxActive = 0U;
		UDS_RxOffset = 0U;
		UDS_RxSN = 0U;
	}

	/* N_As: a short/control frame must be accepted within this time. */
	if ((0U != UDS_CtrlPending) && ((now - UDS_CtrlLastTick) >= UDS_N_AS))
	{
		UDS_CtrlPending = 0U;
		UDS_CtrlLen = 0U;
	}

	if (0U == UDS_TxBusy)
	{
		return;
	}

	if (UDS_TP_TX_WAIT_FC == UDS_TxState)
	{
		/* N_Bs: sender waits for the tester's flow-control frame. */
		if ((now - UDS_TxLastTick) >= UDS_N_BS)
		{
			UDS_CanTpAbort();
		}
	}
	else if (UDS_TP_TX_CF == UDS_TxState)
	{
		/* N_Cs: maximum time allowed to send a consecutive frame. */
		if ((now - UDS_TxLastTick) >= UDS_N_CS)
		{
			UDS_CanTpAbort();
		}
	}
	else if (((UDS_TP_TX_SF == UDS_TxState) || (UDS_TP_TX_FF == UDS_TxState)) &&
	         ((now - UDS_TxLastTick) >= UDS_N_AS))
	{
		UDS_CanTpAbort();
	}

	/* N_Ar and N_Br are receiver-side timing values and are not used by
	 * this ECU transmitter. */
}
