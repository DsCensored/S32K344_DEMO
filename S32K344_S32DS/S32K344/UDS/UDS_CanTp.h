/*
 * UDS_CanTp.h
 *
 *  Created on: 2026年10月23日
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
#define UDS_N_CS	100
#define UDS_N_CR	150

#define UDS_INVFIL 0xAA

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

    /* —— 标准 NRC（0x10 – 0x7F） —— */
    NRC_GENERAL_REJECT = 0x10,
    NRC_SERVICE_NOT_SUPPORTED = 0x11,
    NRC_SUBFUNCTION_NOT_SUPPORTED = 0x12,
    NRC_INCORRECT_MESSAGE_LENGTH_OR_INVALID_FORMAT = 0x13,
    NRC_RESPONSE_TOO_LONG = 0x14,

    /* 0x15 – 0x20: ISO 14229 Reserved */
    NRC_ISO_RESERVED_15_20 = 0x15,   /* 区间起点，仅作占位 */

    NRC_BUSY_REPEAT_REQUEST = 0x21,
    NRC_CONDITIONS_NOT_CORRECT = 0x22,
    /* 0x23: ISO 14229 Reserved */
    NRC_ISO_RESERVED_23 = 0x23,
    NRC_REQUEST_SEQUENCE_ERROR = 0x24,
    NRC_NO_RESPONSE_FROM_SUBNET_COMPONENT = 0x25,
    NRC_FAILURE_PREVENTS_EXECUTION_OF_REQUESTED_ACTION = 0x26,

    /* 0x27 – 0x30: ISO 14229 Reserved */
    NRC_ISO_RESERVED_27_30 = 0x27,   /* 区间起点 */

    NRC_REQUEST_OUT_OF_RANGE = 0x31,
    /* 0x32: ISO 14229 Reserved */
    NRC_ISO_RESERVED_32 = 0x32,
    NRC_SECURITY_ACCESS_DENIED = 0x33,
    NRC_AUTHENTICATION_REQUIRED = 0x34,
    NRC_INVALID_KEY = 0x35,
    NRC_EXCEEDED_NUMBER_OF_ATTEMPTS = 0x36,
    NRC_REQUIRED_TIME_DELAY_NOT_EXPIRED = 0x37,

    /*
     * 0x38 – 0x4F: ISO 14229 Reserved.
     * 注意：0x38 在部分制造商实现中被占用（如 PEAK-System 的 SDIR），
     * 如需严格按 ISO 处理，请整体视为保留区间。
     */
    NRC_ISO_RESERVED_38_4F = 0x38,   /* 区间起点 */

    /* 0x50 – 0x5F: 常作为车辆制造商自定义区间（部分资料列出） */
    NRC_MANUFACTURER_SPECIFIC_50_5F = 0x50,   /* 区间起点 */

    NRC_UPLOAD_DOWNLOAD_NOT_ACCEPTED = 0x70,
    NRC_TRANSFER_DATA_SUSPENDED = 0x71,
    NRC_GENERAL_PROGRAMMING_FAILURE = 0x72,
    NRC_WRONG_BLOCK_SEQUENCE_COUNTER = 0x73,

    NRC_REQUEST_CORRECTLY_RECEIVED_RESPONSE_PENDING = 0x78,
    NRC_SUBFUNCTION_NOT_SUPPORTED_IN_ACTIVE_SESSION = 0x7E,
    NRC_SERVICE_NOT_SUPPORTED_IN_ACTIVE_SESSION = 0x7F,

    /* —— 制造商自定义 / 扩展区间（0x80 – 0xFF） —— */
    NRC_MANUFACTURER_SPECIFIC_START = 0x80,   /* 区间起点 */
    NRC_MANUFACTURER_SPECIFIC_END = 0xFF    /* 区间终点 */
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
