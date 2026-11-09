/*
 * NMmain.c
 *
 *  Created on: 2026年10月30日
 *      Author: CENSORED
 */
#include "NMmain.h"
//#include "ASW_CanCallback.h"
//#include "BSW_CanCallback.h"

unsigned int CANNM_cntAuroSarNMFrameRecvCnt = 0;
unsigned int CANNM_cntAuroSarNMFrameSendCnt = 0;
unsigned char CANNM_stRMSReq = 0;
unsigned char AutoSarNM_stWKUPMode = 0xFF;

static unsigned char NM_GetNmMsgHandle(void)
{

}

void NM_SetNmMsgCycle(unsigned int Cycle)
{

}

void NM_SetNmMsgEnable(unsigned char Enable)
{

}

void NM_SetAppMsgEnable(unsigned char Enable)
{
}

void NM_ResetCanNmSendCnt(void)
{
	CANNM_cntAuroSarNMFrameSendCnt = 0;
	AutoSarNM_stWKUPMode = 0xFF;
}

void NM_PbsmGetWkupMode(unsigned char a)
{

}

void NM_BsmGetWkupMode(void)
{

}


void NM_EnterSleepMode(void)
{

}

void NM_ResetWKUPMode(void)
{

}
