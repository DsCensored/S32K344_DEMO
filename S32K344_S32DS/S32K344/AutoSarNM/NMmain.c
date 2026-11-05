/*
 * NMmain.c
 *
 *  Created on: 2026年10月30日
 *      Author: CENSORED
 */
#include "NMmain.h"
#include "ASW_CanCallback.h"
#include "BSW_CanCallback.h"
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
#include "S32K344_RTC.h"
#include "Rtc_Ip.h"
#include "Rtc_Ip_PBcfg.h"
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

/* RTC periodic wake: 5 min while the ECU is in Standby.  SIRC is used as
 * RTC_CLK (32 kHz), so 5 min = 300 * 32000 ticks. */
#define NM_RTC_CLK_HZ                    (32000U)
#define NM_RTC_WAKE_PERIOD_S             (30U)    /* 5 min */
#define NM_RTC_FORCE_KL15_TIME_10MS      (6000U)   /* 1 min */

#if (NM_RTC_WAKE_ENABLE == STD_ON)
static uint16_t NM_RtcForceKl15Ticks = 0U;
#endif

void NM_RtcWakeupTask10ms(void)
{
#if (NM_RTC_WAKE_ENABLE == STD_ON)
	if (NM_RtcForceKl15Ticks > 0U)
	{
		/* Force KL15 high for 1 min after an RTC wake. */
		BSW_stKL15 = 1U;
		NM_RtcForceKl15Ticks--;
	}
#endif
}

#if (NM_RTC_WAKE_ENABLE == STD_ON)
static void NM_RtcStartWakeTimer(void)
{
	/* Official Rtc_Ip flow: init, enable API interrupt, start one period. */
	Rtc_Ip_Init(0U, &RTC_0_InitConfig_PB);
	Rtc_Ip_EnableInterrupt(0U, RTC_IP_API_INTERRUPT);
	Rtc_Ip_StartTimer(0U, NM_RTC_WAKE_PERIOD_S * NM_RTC_CLK_HZ);
}
#endif

void RtcNotification(void)
{
	/* Gpt channel notification of the generated RTC configuration.
	 * It is intentionally empty: the wake-up reason is only taken from the
	 * ICU/WKPU channel in NM_BsmGetWkupMode(), never from the RTC driver. */
}

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
#if (NM_RTC_WAKE_ENABLE == STD_ON)
	boolean rtcWkpuWakeup;

	/* The ICU/WKPU wake-up channel is the only authority for the RTC wake-up
	 * reason.  There is no retained flag and no RTC status fallback: when the
	 * channel did not latch the event the wake source is treated as not
	 * captured and the CAN / KL15 checks below decide the result. */
	rtcWkpuWakeup = Wkpu_Ip_GetInputState(NM_WKPU_INSTANCE, NM_WKPU_CH_RTC);

	/* RTC periodic wake has priority: force KL15 high for 1 minute so the
	 * network stays active long enough for the application to run. */
	if (rtcWkpuWakeup != FALSE)
	{
		AutoSarNM_stWKUPMode = 0x00U;
		NM_RtcForceKl15Ticks = NM_RTC_FORCE_KL15_TIME_10MS;
		return;
	}
#endif

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
	uint32 wkpuWakeMask;

	NM_SetAppMsgEnable(0U);
	NM_SetNmMsgEnable(0U);

	Wdg_43_Instance0_SetMode(WDGIF_OFF_MODE);
	Wdg_43_Instance0_ClearResetRequest();

	(void)Mcu_InitClock(McuClockSettingConfig_FIRC);

	/* WKPU0_CLK is derived from FIRC/AIPS_SLOW; SIRC is the low-power
	 * standby wake clock.  Keep both alive while the Run domain is off. */
	IP_FIRC->STDBY_ENABLE |= FIRC_STDBY_ENABLE_STDBY_EN_MASK;
	IP_SIRC->MISCELLANEOUS_IN |= SIRC_MISCELLANEOUS_IN_STANDBY_ENABLE_MASK;

	/* NOTE: the RTC must NOT be armed here.  Gpt_StopTimer()/Gpt_DeInit()
	 * further down run over both GPT channels and reach the RTC channel:
	 *   Gpt_StopTimer(1) -> Rtc_Ip_StopTimer() -> APIEN=0 (kills the wake)
	 *   Gpt_DeInit()     -> Rtc_Ip_DeInit()    -> RTCC=0   (kills CNTEN/CLKSEL)
	 * The timer is armed right before Mcu_SetMode() instead. */
	Icu_EnableEdgeDetection(IcuChannel_CAN0RXWKUP);
	Icu_EnableEdgeDetection(IcuChannel_KL15WKUP);
#if (NM_RTC_WAKE_ENABLE == STD_ON)
	Icu_EnableEdgeDetection(IcuChannel_RTCWKUP);
#endif

	/* Only the wake-up request is used in Standby.  The WKPU interrupt is
	 * left masked so a wake-up cannot vector through an unhandled IRQ. */
	wkpuWakeMask = (uint32)(1UL << NM_WKPU_CH_CAN0_RX);
#if (NM_RTC_WAKE_ENABLE == STD_ON)
	wkpuWakeMask |= (uint32)(1UL << NM_WKPU_CH_RTC);
#endif
	IP_WKPU->IRER     &= ~wkpuWakeMask;
	IP_WKPU->IRER_64  &= ~(uint32)(1UL << (NM_WKPU_CH_KL15 - 32U));
	IP_WKPU->WRER     |= wkpuWakeMask;
	IP_WKPU->WRER_64  |= (uint32)(1UL << (NM_WKPU_CH_KL15 - 32U));

	/* No external PMIC PGOOD handshake on this board: without the bypass the
	 * Standby exit waits for PGOOD forever. */
	IP_DCM_GPR->DCMRWF1 |= (uint32)(1UL << 18U);

	Dio_WriteChannel(DioConf_DioChannel_DioChannel_PA29, 1U);
	Dio_WriteChannel(DioConf_DioChannel_DioChannel_PA30, 1U);
	Dio_WriteChannel(DioConf_DioChannel_DioChannel_PA31, 1U);
	Dio_WriteChannel(DioConf_DioChannel_DioChannel_PB18, 1U);

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

	/* Mask all NVIC lines before entering Standby.  The wake-up requests
	 * (WKPU WRER / RTC) are independent of NVIC, and this prevents a stale
	 * CAN/RTC interrupt from vectoring through an undefined handler before
	 * Mcu_SetMode() returns and the reset is executed. */
	{
		uint16 irq;
		for (irq = 0U; irq <= (uint16)SoC_PLL_IRQn; irq++)
		{
			IntCtrl_Ip_DisableIrq((IRQn_Type)irq);
		}
	}

#if (NM_RTC_WAKE_ENABLE == STD_ON)
	/* Arm the RTC-API wake source LAST, after every driver that touches the
	 * RTC (Gpt_StopTimer/Gpt_DeInit) has been shut down.  RTC-API is WKPU
	 * internal wake-up source 0, so only CNTEN/APIEN/APIIE + a clock that
	 * survives Standby (SIRC, STDBY_EN above) are required. */
	NM_RtcStartWakeTimer();
#endif

	(void)Mcu_SetMode(McuModeSettingConf_STANDBY);

	/* Normal (non-fast) Standby resume path.  It is normally not executed:
	 * a Standby exit resets the Run domain and the application restarts from
	 * the reset vector.  The RTC is stopped so it cannot re-trigger before the
	 * reset; the latched WKPU flags are deliberately left untouched because
	 * the ICU/WKPU channel is the only wake-up source and they are what
	 * NM_BsmGetWkupMode() reads after the reset. */
#if (NM_RTC_WAKE_ENABLE == STD_ON)
	IP_RTC->RTCC &= ~(uint32_t)(RTC_RTCC_CNTEN_MASK |
	                            RTC_RTCC_APIEN_MASK |
	                            RTC_RTCC_APIIE_MASK);
	IP_RTC->RTCS = RTC_RTCS_APIF_MASK | RTC_RTCS_RTCF_MASK;
#endif
	S32_SCB->AIRCR = (0x5FAUL << 16) | (1UL << 2);
	Mcu_PerformReset();
}
