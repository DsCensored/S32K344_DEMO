/*
 * UDS_CanTp.h
 *
 *  Created on: 2026??0??3??
 *      Author: CENSORED
 */

#ifndef UDS_CANTP_H_
#define UDS_CANTP_H_

#include "can_fd_message.h"

#define UDS_FUNREQ_ADDR 0x1801FF01
#define UDS_PHYREQ_ADDR 0x1802FF01
#define UDS_RESP_ADDR	0x180101FF

#define UDS_RX_BUF_LEN 1024
#define UDS_TX_BUF_LEN 1024

#define UDS_N_AS	25
#define UDS_N_AR	25
#define UDS_N_BS	150
#define UDS_N_BR	10
#define UDS_N_CS	10
#define UDS_N_CR	150

#define UDS_INVFIL 0xAA

/* ------------------------------------------------------------------------- */
/* CAN / CAN FD                                                              */
/* ------------------------------------------------------------------------- */

/*
 * UDS_CANTP_CANFD_ENABLE
 *   0 = classic CAN: every frame carries 8 bytes (7 diagnostic bytes in a
 *       single frame / consecutive frame, 6 in a first frame).
 *   1 = CAN FD: reception accepts frames up to 64 bytes (long single frame
 *       form with SF_DL up to 62, 63 diagnostic bytes per consecutive frame,
 *       12-bit and 32-bit first frame length), transmission uses frames of
 *       UDS_CANTP_FD_TX_LEN bytes.
 *
 * The CAN controller must be configured for CAN FD as well (EB tresos:
 * CanControllerFdBaudrateConfig with "FD enable" and a data bit rate); this
 * macro only switches the transport layer.  With CAN FD disabled the
 * transport layer keeps the classic 8 byte framing.
 */
#ifndef UDS_CANTP_CANFD_ENABLE
#define UDS_CANTP_CANFD_ENABLE      (0U)
#endif

/*
 * Payload length of every CAN FD frame this ECU transmits.  It also decides
 * how many diagnostic bytes fit into one frame:
 *   single frame      : UDS_CANTP_TX_LEN - 2 (the long form needs a second
 *                       PCI byte; up to 7 bytes the short form is used)
 *   first frame       : UDS_CANTP_TX_LEN - 2
 *   consecutive frame : UDS_CANTP_TX_LEN - 1
 * Valid CAN FD payload lengths are 8, 12, 16, 20, 24, 32, 48 and 64.
 */
#ifndef UDS_CANTP_FD_TX_LEN
#define UDS_CANTP_FD_TX_LEN         (64U)
#endif

#if ((UDS_CANTP_CANFD_ENABLE != 0) && (UDS_CANTP_CANFD_ENABLE != 1))
#error "UDS_CANTP_CANFD_ENABLE must be 0 or 1"
#endif

#if ((UDS_CANTP_FD_TX_LEN < 8U) || (UDS_CANTP_FD_TX_LEN > 64U))
#error "UDS_CANTP_FD_TX_LEN must be 8..64"
#endif

#if ((UDS_CANTP_FD_TX_LEN != 8U) && (UDS_CANTP_FD_TX_LEN != 12U) &&  \
     (UDS_CANTP_FD_TX_LEN != 16U) && (UDS_CANTP_FD_TX_LEN != 20U) && \
     (UDS_CANTP_FD_TX_LEN != 24U) && (UDS_CANTP_FD_TX_LEN != 32U) && \
     (UDS_CANTP_FD_TX_LEN != 48U) && (UDS_CANTP_FD_TX_LEN != 64U))
#error "UDS_CANTP_FD_TX_LEN is not a valid CAN FD payload length"
#endif

#if (UDS_CANTP_CANFD_ENABLE != 0)
/* Length of every transmitted frame. */
#define UDS_CANTP_TX_LEN            (UDS_CANTP_FD_TX_LEN)
/* Largest single frame payload: the short form carries up to 7 bytes, above
 * that the second PCI byte is needed. */
#define UDS_CANTP_SF_MAX            (((UDS_CANTP_TX_LEN) > 8U) ? ((UDS_CANTP_TX_LEN) - 2U) : 7U)
#else
#define UDS_CANTP_TX_LEN            (8U)
#define UDS_CANTP_SF_MAX            (7U)
#endif

typedef struct{
	unsigned char SID;
	unsigned int Len;
	unsigned char* Buf;
	/* Addressing mode of the current request:
	 * UDS_DCM_ADDRESS_PHYSICAL or UDS_DCM_ADDRESS_FUNCTIONAL. */
	unsigned char Addr;
}UDS_TpStr;

typedef enum {
	kSF = 0x00,
	kFF = 0x10,
	kCF = 0x20,
	kFC = 0x30,
	kMask = 0xF0
}UDS_FrameType;

typedef enum {
    NRC_POSITIVE_RESPONSE = 0x00,

    /* ??????? NRC??x10 ??0x7F??????*/
    NRC_GENERAL_REJECT = 0x10,
    NRC_SERVICE_NOT_SUPPORTED = 0x11,
    NRC_SUBFUNCTION_NOT_SUPPORTED = 0x12,
    NRC_INCORRECT_MESSAGE_LENGTH_OR_INVALID_FORMAT = 0x13,
    NRC_RESPONSE_TOO_LONG = 0x14,

    /* 0x15 ??0x20: ISO 14229 Reserved */
    NRC_ISO_RESERVED_15_20 = 0x15,   /* ??????????????*/

    NRC_BUSY_REPEAT_REQUEST = 0x21,
    NRC_CONDITIONS_NOT_CORRECT = 0x22,
    /* 0x23: ISO 14229 Reserved */
    NRC_ISO_RESERVED_23 = 0x23,
    NRC_REQUEST_SEQUENCE_ERROR = 0x24,
    NRC_NO_RESPONSE_FROM_SUBNET_COMPONENT = 0x25,
    NRC_FAILURE_PREVENTS_EXECUTION_OF_REQUESTED_ACTION = 0x26,

    /* 0x27 ??0x30: ISO 14229 Reserved */
    NRC_ISO_RESERVED_27_30 = 0x27,   /* ?????? */

    NRC_REQUEST_OUT_OF_RANGE = 0x31,
    /* 0x32: ISO 14229 Reserved */
    NRC_ISO_RESERVED_32 = 0x32,
    NRC_SECURITY_ACCESS_DENIED = 0x33,
    NRC_AUTHENTICATION_REQUIRED = 0x34,
    NRC_INVALID_KEY = 0x35,
    NRC_EXCEEDED_NUMBER_OF_ATTEMPTS = 0x36,
    NRC_REQUIRED_TIME_DELAY_NOT_EXPIRED = 0x37,

    /*
     * 0x38 ??0x4F: ISO 14229 Reserved.
     * ?????x38 ????????????????????? PEAK-System ??SDIR???
     * ????????ISO ????????????????????
     */
    NRC_ISO_RESERVED_38_4F = 0x38,   /* ?????? */

    /* 0x50 ??0x5F: ????????????????????????????????*/
    NRC_MANUFACTURER_SPECIFIC_50_5F = 0x50,   /* ?????? */

    NRC_UPLOAD_DOWNLOAD_NOT_ACCEPTED = 0x70,
    NRC_TRANSFER_DATA_SUSPENDED = 0x71,
    NRC_GENERAL_PROGRAMMING_FAILURE = 0x72,
    NRC_WRONG_BLOCK_SEQUENCE_COUNTER = 0x73,

    NRC_REQUEST_CORRECTLY_RECEIVED_RESPONSE_PENDING = 0x78,
    NRC_SUBFUNCTION_NOT_SUPPORTED_IN_ACTIVE_SESSION = 0x7E,
    NRC_SERVICE_NOT_SUPPORTED_IN_ACTIVE_SESSION = 0x7F,

    /* ??????????????/ ????????x80 ??0xFF??????*/
    NRC_MANUFACTURER_SPECIFIC_START = 0x80,   /* ?????? */
    NRC_MANUFACTURER_SPECIFIC_END = 0xFF    /* ?????? */
} UdsNrc_t;

extern CAN_FD_MESSAGE UDS_FunReq, UDS_PhyReq, UDS_Resp;
extern unsigned char UDS_ReqFlg;
extern unsigned char UDS_RxBuf[UDS_RX_BUF_LEN];
extern unsigned char UDS_TxBuf[UDS_TX_BUF_LEN];
extern UDS_TpStr UDS_RxReq;

void UDS_CanTpInit(void);
void UDS_CanTpRxCallback(CAN_FD_MESSAGE* Data);

/*
 * Queue a negative response.  If the current request (UDS_RxReq) was
 * received on functional addressing, the NRC is discarded.
 */
void UDS_CanSendNRC(unsigned char SID, UdsNrc_t NRC);

/*
 * Same as UDS_CanSendNRC(), but with an explicit addressing mode
 * (UDS_DCM_ADDRESS_PHYSICAL / UDS_DCM_ADDRESS_FUNCTIONAL) instead of the
 * addressing mode of the most recently received request.  Used for responses
 * that belong to an older request, e.g. the final response after NRC 0x78.
 */
void UDS_CanSendNRCForAddr(unsigned char SID, UdsNrc_t NRC, unsigned char addr);

/*
 * Fill UDS_TxBuf[0..len-1] first, then call UDS_CanTpSend(len).
 * While UDS_CanTpIsTxBusy() is non-zero, UDS_TxBuf is not used by the
 * transport layer; the active request is copied into a private buffer.
 */
unsigned char UDS_CanTpSend(unsigned int len);
unsigned char UDS_CanTpIsTxBusy(void);
void UDS_CanTpAbort(void);

/* Call these from the 5 ms task. */
void UDS_CanTpTxPoll(void);
void UDS_CanTpTimerPoll(void);

#endif /* UDS_CANTP_H_ */


