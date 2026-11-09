/*
 * NMmain.c
 *
 *  Created on: 2026年10月30日
 *      Author: CENSORED
 */
#include "NMmain.h"
#include "ASW_CanCallback.h"
#include "BSW_CanCallback.h"
#include "Nvm.h"
#include "RTE.h"
#include "Mcu.h"
#include "Icu.h"
#include "Icu_Cfg.h"
#include "Gpt.h"
#include "Gpt_Cfg.h"
#include "Can_43_FLEXCAN.h"
#include "CDD_Uart.h"
#include "Dma_Ip.h"
#include "Wkpu_Ip.h"
#include "S32K344_FXOSC.h"
#include "S32K344_FIRC.h"
#include "S32K344_SIRC.h"
#include "S32K344_DCM_GPR.h"
#include "IntCtrl_Ip.h"
#include "Dio.h"
#include "Dio_Cfg.h"
#include "Wdg_CfgExt.h"
#include "WdgIf_Types.h"
#include "Wdg_43_Instance0.h"
#include "Wdg_ChannelTypes.h"
#include "Wdg_Channel.h"

unsigned int CANNM_cntAuroSarNMFrameRecvCnt = 0;
unsigned int CANNM_cntAuroSarNMFrameSendCnt = 0;
unsigned char CANNM_stRMSReq = 0;
unsigned char AutoSarNM_stWKUPMode = 0xFF;

/* Activity counter consumed by the AutoSarNm model to keep the bus awake
 * for AUROSAR_NM_UDS_OVERTIME after the last diagnostic request. */
uint8_T AutoSarNM_cntUDSMsgRxCnt = 0U;

static unsigned char NM_GetNmMsgHandle(void)
{
	unsigned char res = 0xFF;
	for(unsigned char i = 0; i < CAN_CHN1_TX_COUNT; i++)
	{
		if(CANNM_CHN[i].Data->ID == CAN_ID_VEH_TX_NM)
		{
			res = i;
			break;
		}
	}

	return res;
}

void NM_SetNmMsgCycle(unsigned int Cycle)
{
	unsigned char NmMsgHandle = NM_GetNmMsgHandle();

	if(0xFF == NmMsgHandle)return;

	CANNM_CHN[NmMsgHandle].send_period = Cycle;
}

void NM_SetNmMsgEnable(unsigned char Enable)
{
	unsigned char NmMsgHandle = NM_GetNmMsgHandle();

	if(0xFF == NmMsgHandle)return;

	CANNM_CHN[NmMsgHandle].Enable = ((0 == Enable) ? 0 : 1);
}

void NM_SetAppMsgEnable(unsigned char Enable)
{
	unsigned char NmMsgHandle = NM_GetNmMsgHandle();
	unsigned char res = ((0 == Enable) ? 0 : 1);

	for(unsigned char i = 0; i < CAN_CHN1_TX_COUNT; i++)
	{
		if(i == NmMsgHandle)continue;
		CANNM_CHN[i].Enable = res;
	}
}

void NM_ResetCanNmSendCnt(void)
{
	CANNM_cntAuroSarNMFrameSendCnt = 0;
}

void NM_ResetWKUPMode(void)
{
	AutoSarNM_stWKUPMode = 0xFF;
}

void NM_PbsmGetWkupMode(unsigned char reset)
{
	static unsigned int CANNM_cntAuroSarNMFrameRecvCnt_Delay = 0;
	if(reset != 0)
	{
		CANNM_cntAuroSarNMFrameRecvCnt_Delay = CANNM_cntAuroSarNMFrameRecvCnt;
		return;
	}

	if(AutoSarNM_stWKUPMode != 0xFF)return;

	//产生主动唤醒
	if(BSW_stKL15 != 0)
	{
		AutoSarNM_stWKUPMode = 0x00;
	}
	//产生被动唤醒
	else if(CANNM_cntAuroSarNMFrameRecvCnt_Delay != CANNM_cntAuroSarNMFrameRecvCnt)
	{
		CANNM_cntAuroSarNMFrameRecvCnt_Delay = CANNM_cntAuroSarNMFrameRecvCnt;
		AutoSarNM_stWKUPMode = 0x01;
	}
}

void NM_BsmGetWkupMode(void)
{
	boolean canWakeup;
	boolean kl15Wakeup;

	/* This function runs from AutoSarNm_initialize(), i.e. before Icu_Init().
	 * At this point IRER is still zero, so Wkpu_Ip_GetInputState() only tests
	 * the latched WISR bits and clears them.  Reading the flags before the ICU
	 * driver re-initializes WKPU preserves the standby wake-up reason. */
	canWakeup  = Wkpu_Ip_GetInputState(NM_WKPU_INSTANCE, NM_WKPU_CH_CAN0_RX);
	kl15Wakeup = Wkpu_Ip_GetInputState(NM_WKPU_INSTANCE, NM_WKPU_CH_KL15);

	/* KL15 is the active wake-up source and has priority over CAN.  A cold
	 * start (ignition already on) has no WKPU edge, so also sample the pin
	 * level through Port/Dio. */
	if ((kl15Wakeup != FALSE) ||
	    (Dio_ReadChannel(DioConf_DioChannel_DioChannel_PB19) != 0U))
	{
		AutoSarNM_stWKUPMode = 0x00U;
	}
	else if (canWakeup != FALSE)
	{
		AutoSarNM_stWKUPMode = 0x01U;
	}
	else
	{
		AutoSarNM_stWKUPMode = 0xFFU;
	}
}

void NM_CanNmSendCallBack(CAN_TX_BufType *buf)
{
	(void)buf;
	CANNM_cntAuroSarNMFrameSendCnt++;
}

void NM_CanNmRecvCallBack(CAN_FD_MESSAGE *Data)
{
	if (Data->Length > CANNM_CBV_POSITION)
	{
		if((Data->Data[CANNM_CBV_POSITION] & 0x01) == 1)
		{
			CANNM_stRMSReq++;
		}
	}

	CANNM_cntAuroSarNMFrameRecvCnt++;
}

void NM_NotifyUdsActivity(void)
{
	AutoSarNM_cntUDSMsgRxCnt++;
}

void WKPU_IRQHandler(void)
{
	/* Clear all WKPU flags and restart the application.  This handler is only
	 * enabled just before entering Standby; it makes a normal (resume) exit
	 * behave like a reset-on-wake. */
	IP_WKPU->WISR = 0xFFFFFFFFU;
	IP_WKPU->WISR_64 = 0xFFFFFFFFU;
	Mcu_PerformReset();
}

void NM_EnterSleepMode(void)
{
	uint8 i;

	/* Before the power is removed (Standby): read every NVM block back from
	 * the DFlash, compare it with the RAM shadow and write it again until
	 * both are identical. */
	(void)Nvm_Flush();

	NM_SetAppMsgEnable(0U);
	NM_SetNmMsgEnable(0U);

	Wdg_43_Instance0_SetMode(WDGIF_OFF_MODE);
	Wdg_43_Instance0_ClearResetRequest();

	(void)Mcu_InitClock(McuClockSettingConfig_FIRC);

	/* WKPU0_CLK is derived from FIRC/AIPS_SLOW; SIRC is the low-power
	 * standby wake clock.  Keep both alive while the Run domain is off. */
	IP_FIRC->STDBY_ENABLE |= FIRC_STDBY_ENABLE_STDBY_EN_MASK;
	IP_SIRC->MISCELLANEOUS_IN |= SIRC_MISCELLANEOUS_IN_STANDBY_ENABLE_MASK;

	Icu_EnableEdgeDetection(IcuChannel_CAN0RXWKUP);
	Icu_EnableEdgeDetection(IcuChannel_KL15WKUP);

	/* Only the wake-up request is used in Standby.  The WKPU interrupt is
	 * left masked so a wake-up cannot vector through an unhandled IRQ. */
	IP_WKPU->IRER     &= ~(uint32)(1UL << NM_WKPU_CH_CAN0_RX);
	IP_WKPU->IRER_64  &= ~(uint32)(1UL << (NM_WKPU_CH_KL15 - 32U));
	IP_WKPU->WRER     |= (uint32)(1UL << NM_WKPU_CH_CAN0_RX);
	IP_WKPU->WRER_64  |= (uint32)(1UL << (NM_WKPU_CH_KL15 - 32U));

	/* No external PMIC PGOOD handshake on this board: without the bypass the
	 * Standby exit waits for PGOOD forever. */
	IP_DCM_GPR->DCMRWF1 |= (uint32)(1UL << 18U);

	Dio_WriteChannel(DioConf_DioChannel_DioChannel_PA29, 1U);

	for(i = 0; i < GPT_NUM_CONFIG; i++)
	{
		Gpt_StopTimer(i);
	}
	Gpt_DeInit();

	for(i = 0; i < CAN_43_FLEXCAN_CONTROLLER_CONFIG_COUNT; i++)
	{
		(void)Can_43_FLEXCAN_SetControllerMode(i, CAN_CS_STOPPED);
	}
	Can_43_FLEXCAN_DeInit();

	Uart_Deinit();
	(void)Dma_Ip_Deinit();

	(void)Mcu_SetMode(McuModeSettingConf_STANDBY);

	/* Normal (non-fast) Standby resumes here after a wake-up.  Restart the
	 * MCU so BSW/peripherals are initialized again instead of returning into
	 * the half-shut-down application. */
	Mcu_PerformReset();
}
