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
#include "BSW_UART.h"

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
}

static void MCAL_Init(void)
{
	MCAL_MCU_Init();
	MCAL_PORT_Init();
	MCAL_Platform_Init();
	MCAL_GPT_Init();
	MCAL_CAN_Init();
	MCAL_UART_Init();
}

void BSW_Init(void)
{
	MCAL_Init();

	/* UART 驱动初始化完成后，启动 BSW 层的接收 */
	BSW_UART6_Init();
}

