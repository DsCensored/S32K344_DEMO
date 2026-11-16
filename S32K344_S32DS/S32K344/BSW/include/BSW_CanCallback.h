/*
 * BSW_CanCallback.h
 *
 *  Created on: 2026年10月22日
 *      Author: CENSORED
 */

#ifndef INCLUDE_BSW_CANCALLBACK_H_
#define INCLUDE_BSW_CANCALLBACK_H_

#include "can_fd_message.h"

typedef struct CAN_RXStr CAN_RXStr;
typedef void (*CAN_RX_CallbackType)(CAN_FD_MESSAGE* data);

struct CAN_RXStr
{
	unsigned int canid;
	CAN_FD_MESSAGE *Data;
	unsigned int get_time_tick;
	unsigned int overtime;
	CAN_RX_CallbackType Callback;
	volatile unsigned char *timeout_flag;
	/* 0x28 CommunicationControl override: 1 = reception of this frame is
	 * disabled.  Zero-initialised, so every existing entry stays enabled. */
	unsigned char CommCtrlDisabled;
};

typedef struct
{
	CAN_FD_MESSAGE Message;
} CAN_TX_BufType;

typedef struct CAN_TXStr CAN_TXStr;
typedef void (*CAN_TX_CallbackType)(CAN_TX_BufType *buf);

struct CAN_TXStr
{
	CAN_FD_MESSAGE *Data;
	unsigned int send_period;
	unsigned int last_send_tick;
	CAN_TX_CallbackType Callback;
	unsigned char Enable;
	/* 0x28 CommunicationControl override: 1 = transmission of this frame is
	 * disabled.  Kept separate from Enable so the NM state machine can keep
	 * updating Enable without overriding the diagnostic request. */
	unsigned char CommCtrlDisabled;
};

void BSW_CANRX_INIT(CAN_RXStr* str, unsigned char len);
void BSW_CANRX_TIMEOUT_POLL(void);
void BSW_CANTX_POLL(void);
void BSW_CAN_BUSOFF_POLL(void);

/**
 * @brief Transmit the first periodic frame(s) before the OS is started.
 *
 * ASW_CanInit() sets last_send_tick to an "old" value, so every TX entry is
 * due on the first poll: the periodic frames leave the ECU immediately after
 * the CAN controller has been started, without waiting for the first 5 ms
 * task.  The period bookkeeping continues from this timestamp, i.e. the next
 * frame is due one send_period later (no burst when the task loop starts).
 *
 * Must be called after BSW_InitEarly() and ASW_Init().
 */
void BSW_CAN_SendFirstFrame(void);

#endif /* INCLUDE_BSW_CANCALLBACK_H_ */
