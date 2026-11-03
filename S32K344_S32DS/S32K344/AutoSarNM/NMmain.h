/*
 * NMmain.h
 *
 *  Created on: 2026年10月30日
 *      Author: CENSORED
 */

#ifndef NMMAIN_H_
#define NMMAIN_H_

#include "BSW_CanCallback.h"

#define CANNM_CHN								CAN_CHN1_TX

/* WKPU hardware channels used as standby wake-up sources.
 * Keep these in sync with S43K344_EB/config/Icu.xdm and Port.xdm:
 *   CAN0_RX -> WKPU channel 19 (CAN0 RX pin)
 *   KL15    -> WKPU channel 43 (PB19, Port mode WKPU_WKPU_43_IN)
 */
#define NM_WKPU_INSTANCE						(0U)
#define NM_WKPU_CH_CAN0_RX						(19U)
#define NM_WKPU_CH_KL15							(42U)

#define CAN_ID_VEH_RX_NM                        (402784010)        	//整车CAN网络管理的接收报文ID(低8位掩码屏蔽不关心) 或 两者皆可(CAN_TYPE_STD_FD)

#define CAN_ID_VEH_TX_NM                        (0x18EEFF02)       			//整车CAN网络管理的发送报文ID

#define CANNM_REPEAT_MEG_TIME                   	3300U         //重复报文状态的持续时间, 单位-毫秒
#define CANNM_IMMEDIATE_TRANSMISSION_COUNT      	10U           //重复报文状态中, 直接发送报文的个数
#define CANNM_IMMEDIATE_CYCLE_TIME	              	20U           //重复报文状态中，直接发送报文的周期, 单位-毫秒
#define CANNM_NM_TIMEOUT_TIME	                   	2500U         //NM_Timeout超时时间, 单位-毫秒
#define CANNM_MSG_CYCLE_TIME			            1000U          //网络管理报文周期,单位-毫秒
#define CANNM_MSG_TIMEOUT_TIME			            2000U         //网张管理报文发送超时判断时间，单位-毫秒
#define CANNM_CBV_POSITION			                1U//1U            //控制比特向量所在的字节位置，计数从0开始
#define CANNM_NODE_ID_POSITION				        0U            //节点ID所在的字节位置，计数从0开始
#define CANNM_WAIT_BUS_SLEEP_TIME				    1500U         //等待睡眠时间,单位毫秒

#define CANNM_BSM_WAITSLP_TIME				    	500U         //等待睡眠时间,单位毫秒

#define AUROSAR_NM_UDS_OVERTIME						5000U

void NM_SetNmMsgCycle(unsigned int Cycle);

void NM_SetNmMsgEnable(unsigned char Enable);

void NM_SetAppMsgEnable(unsigned char Enable);

void NM_ResetCanNmSendCnt(void);

void NM_PbsmGetWkupMode(unsigned char reset);

void NM_EnterSleepMode(void);

void NM_BsmGetWkupMode(void);

void NM_CanNmRecvCallBack(CAN_FD_MESSAGE *Data);

void NM_CanNmSendCallBack(CAN_TX_BufType *buf);

void NM_NotifyUdsActivity(void);

#endif /* NMMAIN_H_ */
