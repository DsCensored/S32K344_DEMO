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
#include "Nvm.h"
#include "hse_trng.h"   /* HSE 硬件 TRNG 自检 / UDS 0x27 种子 */

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

static void MCAL_NVM_Init(void)
{
	/* Shared NVM area in the internal data flash (4 x 256 bytes). */
	if (E_NOT_OK == Nvm_Init())
	{
		/* Blocks stay usable (zero-filled) even if the Fee start-up failed. */
	}

	(void)Nvm_GetStatus();
}

static void MCAL_Init(void)
{
	MCAL_MCU_Init();
	MCAL_PORT_Init();
	MCAL_Platform_Init();
	MCAL_GPT_Init();
	MCAL_CAN_Init();
	MCAL_NVM_Init();
}

void BSW_Init(void)
{
	MCAL_Init();

#if (HSE_TRNG_SELFTEST_ON_BOOT != 0U)
	/* HSE TRNG 上电自检：结果存入 g_hseTrngSelfTest[]（调试器可直接读），
	 * 用来确认 HSE 随机数服务可用；量产可把 HSE_TRNG_SELFTEST_ON_BOOT 置 0。 */
	(void)HseTrng_SelfTest();
#endif
}

