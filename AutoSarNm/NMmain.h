/*
 * NMmain.c
 *
 *  Created on: 2026年10月30日
 *      Author: CENSORED
 */

static unsigned char NM_GetNmMsgHandle(void);

void NM_SetNmMsgCycle(unsigned int Cycle);

void NM_SetNmMsgEnable(unsigned char Enable);

void NM_SetAppMsgEnable(unsigned char Enable);

void NM_ResetCanNmSendCnt(void);

void NM_PbsmGetWkupMode(unsigned char a);

void NM_EnterSleepMode(void);

void NM_BsmGetWkupMode(void);

void NM_ResetWKUPMode(void);
