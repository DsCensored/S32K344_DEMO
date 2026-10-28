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
};

void BSW_CANRX_INIT(CAN_RXStr* str, unsigned char len);
void BSW_CANRX_TIMEOUT_POLL(void);
void BSW_CANTX_POLL(void);
void BSW_CAN_BUSOFF_POLL(void);

#endif /* INCLUDE_BSW_CANCALLBACK_H_ */
