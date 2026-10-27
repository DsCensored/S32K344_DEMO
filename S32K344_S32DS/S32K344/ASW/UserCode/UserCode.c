/*
 * UserCode.c
 *
 *  Created on: 2026年10月20日
 *      Author: CENSORED
 */
#include "UserCode.h"
#include "UserOs.h"
#include "ASW_CanCallback.h"
#include "BSW_CanCallback.h"
#include "BSW_UART.h"
#include "can_fd_message.h"
#include "SchM_Can_43_FLEXCAN.h"
#include "UDS_CanTp.h"
#include "UDS_DCM.h"

static void ASW_userTask2ms(void)
{
	Can_43_FLEXCAN_MainFunction_Read();
	Can_43_FLEXCAN_MainFunction_Write();
	Can_43_FLEXCAN_MainFunction_Mode();
	BSW_UART6_RxPoll();
}
static void ASW_userTask5ms(void)
{
	BSW_CAN_BUSOFF_POLL();
	BSW_CANTX_POLL();
	UDS_CanTpTimerPoll();
	UDS_CanTpTxPoll();
}
static void ASW_userTask10ms(void)
{
	BSW_CANRX_TIMEOUT_POLL();
}
static void ASW_userTask20ms(void)
{

}
static void ASW_userTask50ms(void)
{

}
static void ASW_userTask100ms(void)
{
	UDS_DCM_Poll();
}
static void ASW_userTask1000ms(void)
{
//	TX_DATA1.Data[0] = BSW_GetCpuLoad();
	unsigned char data[] = "Hallo world";
	BSW_UART6_Send(data,sizeof(data));
}

void ASW_Init(void)
{
	BSW_CANRX_INIT(CAN_CHN1_RX, (unsigned char)CAN_CHN1_RX_COUNT);
	UDS_CanTpInit();
	UDS_DCM_Init();

	BSW_RegTask(2, 0, ASW_userTask2ms);
	BSW_RegTask(5, 1, ASW_userTask5ms);
	BSW_RegTask(10, 2, ASW_userTask10ms);
	BSW_RegTask(20, 2, ASW_userTask20ms);
	BSW_RegTask(50, 3, ASW_userTask50ms);
	BSW_RegTask(100, 3, ASW_userTask100ms);
	BSW_RegTask(1000, 4, ASW_userTask1000ms);
}

