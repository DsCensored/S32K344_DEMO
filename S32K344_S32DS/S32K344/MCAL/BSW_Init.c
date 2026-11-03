/*
 * BSW_Init.c
 *
 *  Created on: 2026年10月20日
 *      Author: CENSORED
 */

#include "Gpt.h"
#include "Platform.h"
#include "Port.h"
#include "MCU.h"
#include "Can_43_FLEXCAN.h"
#include "CanIf.h"
#include "CDD_Uart.h"
#include "Dma_Ip.h"
#include "Dma_Mux_Ip.h"
#include "S32K344_SWT.h"
#include "BSW_UART.h"
#include "Nvm.h"
#include "AutoSarNm.h"
#include "Icu.h"
#include "Icu_PBcfg.h"
#include "Wdg_CfgExt.h"
#include "WdgIf_Types.h"
#include "Wdg_43_Instance0.h"
#include "Wdg_ChannelTypes.h"
#include "Wdg_Channel.h"

static void MCAL_MCU_Init(void)
{
#if (STD_ON == MCU_PRECOMPILE_SUPPORT)
    Mcu_Init(NULL_PTR);
#else
    Mcu_Init(&Mcu_Config);
#endif

#if (STD_ON == MCU_INIT_CLOCK)
    /* Initialize the clock tree and apply PLL as system clock */
    Mcu_InitClock(McuClockSettingConfig_160Mhz);

    #if (MCU_NO_PLL == STD_OFF)
        /* Wait until PLL is locked */
        while ( MCU_PLL_LOCKED != Mcu_GetPllStatus() )
        {
            /* Busy wait until the System PLL is locked */
        }
        Mcu_DistributePllClock();
    #endif

    /* Apply a mode configuration */
    Mcu_SetMode(McuModeSettingConf_0);

    /* Read and clear MC_RGM.RDSS.  On a normal (non-fast) standby exit the
     * device resumes from WFI; if the standby-reset flags are left set the
     * next standby sequence can be affected.  This call also lets upper layers
     * distinguish a wake-up reset from a cold start. */
    (void)Mcu_GetResetReason();
#else
    #error "The Mcu Init Clock API should be enabled from the Mcu driver"
#endif
}

static void MCAL_Platform_Init(void)
{
	//Formal para invalid
	Platform_Init(NULL_PTR);
}

static void MCAL_GPT_Init(void)
{
	Gpt_Init(&Gpt_Config);
	Gpt_EnableNotification(GptConf_GptChannelConfiguration_GptChannelConfiguration_PIT0_CH0);
	//39999比40000的OS精度更高
	Gpt_StartTimer(GptConf_GptChannelConfiguration_GptChannelConfiguration_PIT0_CH0, 39999);
}

static void MCAL_PORT_Init(void)
{
	Port_Init(&Port_Config);
}

static void MCAL_WDG_Init(void)
{
	Wdg_43_Instance0_Init(&Wdg_Config_0);
	Wdg_43_Instance0_SetMode(WDGIF_SLOW_MODE);
}

static void MCAL_CAN_Init(void)
{
	CanIf_Init(NULL_PTR);

#if (CAN_43_FLEXCAN_PRECOMPILE_SUPPORT == STD_ON)
    Can_43_FLEXCAN_Init(NULL_PTR);
#else
    Can_43_FLEXCAN_Init(&Can_43_FLEXCAN_Config);
#endif

    /* Start Can controller */
    Can_43_FLEXCAN_SetControllerMode(Can_43_FLEXCANConf_CanController_CanController_0, CAN_CS_STARTED);
}

static void MCAL_UART_Init(void)
{
	/* DMA must be initialized before the UART driver:
	 * LPUART/FlexIO UART request DMA transfers through Dma_Ip directly. */
	if (DMA_IP_STATUS_SUCCESS != Dma_Ip_Init(&Dma_Ip_xDmaInitPB))
	{
		/* TODO: report DET / project specific error handling */
	}

	/* DMAMUX routing (LPUART TX/RX request -> DMA channel) */
	Dma_Mux_Ip_Init(&Dma_Mux_Config);

	/* UART_PRECOMPILE_SUPPORT == STD_OFF, so the generated config must be passed */
	Uart_Init(&Uart_xConfig);

	/* UART 驱动初始化完成后，启动 BSW 层的接收 */
	BSW_UART6_Init();
}

static void MCAL_NVM_Init(void)
{
	/* Fee + MemAcc + Mem_43_INFLS live in the internal data flash.
	 * Nvm_Init() blocks until Fee has finished its start-up scan and the
	 * four 256-byte blocks are loaded into RAM. */
	if (E_NOT_OK == Nvm_Init())
	{
		/* The blocks are still usable (zero-filled), but report the issue. */
	}

	(void)Nvm_GetStatus();
}

static void MCAL_ICU_Init(void)
{
	Icu_Init(&Icu_Config);
}

static void MCAL_Init(void)
{
	MCAL_MCU_Init();
	MCAL_PORT_Init();
	MCAL_Platform_Init();
	MCAL_GPT_Init();
	MCAL_CAN_Init();
	MCAL_UART_Init();
	MCAL_NVM_Init();
	AutoSarNm_initialize();//获取ICU唤醒原因需要在ICU初始化之前执行
	MCAL_ICU_Init();
}

void BSW_Init(void)
{
	MCAL_Init();
	/* Start the watchdog only after all blocking MCAL/NVM initialization is
	 * complete, so a long startup cannot trip the 10 s timeout. */
	MCAL_WDG_Init();
}

