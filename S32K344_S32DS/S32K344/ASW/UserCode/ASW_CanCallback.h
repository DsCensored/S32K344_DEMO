/*
 * ASW_CanCallback.h
 *
 *  Created on: 2026年10月22日
 *      Author: CENSORED
 */

#ifndef USERCODE_ASW_CANCALLBACK_H_
#define USERCODE_ASW_CANCALLBACK_H_

#include "BSW_CanCallback.h"

#define CAN_CHN1_RX_COUNT 3U
#define CAN_CHN1_TX_COUNT 1U

extern CAN_FD_MESSAGE TX_DATA1;
extern CAN_RXStr CAN_CHN1_RX[CAN_CHN1_RX_COUNT];
extern CAN_TXStr CAN_CHN1_TX[CAN_CHN1_TX_COUNT];

#endif /* USERCODE_ASW_CANCALLBACK_H_ */
