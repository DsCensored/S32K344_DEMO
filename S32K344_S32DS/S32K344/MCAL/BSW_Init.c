/*
 * BSW_Init.c
 *
 *  Created on: 2026年10月20日
 *      Author: CENSORED
 *
 *  See BSW_Init.h: the start-up is split in an "early" part that makes the
 *  first CAN frame possible and a "late" part that runs in a low-priority
 *  1 ms task.  The cooperative OS must never be blocked for several
 *  milliseconds while the periodic CAN frames are being sent, so the longest
 *  item (the Fee/NVM start-up scan) is executed slice by slice through
 *  Nvm_InitStep().
 */

#include "BSW_Init.h"

#include "UserOS.h"      /* BSW_RegTask / BSW_UnregTask / BSW_TASK_HANDLE_INVALID */
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
#include "Nvm.h"
#include "UDS_DemCfg.h"  /* DEM initialisation needs the NVM shadow            */
#include "AutoSarNm.h"
#include "Icu.h"
#include "Icu_PBcfg.h"
#include "BSW_WDG.h"     /* guarded watchdog wrapper                       */

/* Steps of the deferred initialisation.  The watchdog comes first: it is a
 * safety item and must be re-configured as early as possible, whereas the
 * NVM scan runs for a few hundred milliseconds and the UART is not needed
 * until the 1 s task sends its first string.  The DEM follows the NVM: it
 * restores the DTC memory from the NVM shadow. */
#define BSW_LATE_STEP_WDG       (0U)
#define BSW_LATE_STEP_ICU       (1U)
#define BSW_LATE_STEP_NVM       (2U)   /* Nvm_InitStep() until it is done      */
#define BSW_LATE_STEP_DEM       (3U)   /* UDS_AppDemInit()                     */
#define BSW_LATE_STEP_UART      (4U)
#define BSW_LATE_STEP_DONE      (5U)

static uint8 BSW_LateStep = BSW_LATE_STEP_WDG;
static uint8 BSW_stInitDone = 0U;

/* Debug view of the deferred start-up (J-Link watch, no side effects):
 *   g_bswInitLateStep : current step (0 WDG, 1 ICU, 2 NVM, 3 DEM, 4 UART,
 *                       5 done)
 *   g_bswInitLateNvm  : 1 once Nvm_InitStep() reported DONE
 *   g_bswInitDone     : 1 once the whole sequence (incl. UART) finished
 * A step that is never reached (e.g. the NVM start-up being skipped) is
 * visible here instead of staying silently unnoticed. */
volatile uint8 g_bswInitLateStep = BSW_LATE_STEP_WDG;
volatile uint8 g_bswInitLateNvm  = 0U;
volatile uint8 g_bswInitDone     = 0U;

/* Handle of the deferred-init task, so it can remove itself when done. */
static unsigned char BSW_LateTaskHandle = BSW_TASK_HANDLE_INVALID;

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

static void MCAL_ICU_Init(void)
{
	Icu_Init(&Icu_Config);
}

void BSW_InitEarly(void)
{
	MCAL_MCU_Init();
	MCAL_PORT_Init();
	MCAL_Platform_Init();
	MCAL_GPT_Init();   /* 1 ms tick: BSW_GetSystemTick() drives the TX periods */
	MCAL_CAN_Init();
}

void BSW_InitLateTask(void)
{
	g_bswInitLateStep = BSW_LateStep;

	switch (BSW_LateStep)
	{
	case BSW_LATE_STEP_WDG:
		/* Guarded wrapper (BSW_WDG.c): after this the cyclic BSW_WDG_Service()
		 * calls are effective, before it they are counted no-ops. */
		BSW_WDG_Init();
		BSW_LateStep = BSW_LATE_STEP_ICU;
		break;

	case BSW_LATE_STEP_ICU:
		/* AutoSarNm_initialize() already ran in ASW_Init(): the NM wake-up
		 * reason must be captured before the ICU is initialised. */
		MCAL_ICU_Init();
		BSW_LateStep = BSW_LATE_STEP_NVM;
		break;

	case BSW_LATE_STEP_NVM:
		/* Non-blocking: one slice per scheduler cycle, ~150 us each.  Errors
		 * are tolerated (the blocks then read as 0xFF), the remaining
		 * start-up proceeds either way. */
		if (NVM_INIT_STEP_BUSY == Nvm_InitStep())
		{
			break;
		}
		if (TRUE == Nvm_IsInitialised())
		{
			g_bswInitLateNvm = 1U;
		}
		BSW_LateStep = BSW_LATE_STEP_DEM;
		break;

	case BSW_LATE_STEP_DEM:
		/* The DEM restores the DTC memory of the last power cycle, ages the
		 * DTCs that did not fail and opens a new operation cycle.  It needs
		 * the NVM shadow, so it runs after the NVM step; without a valid NVM
		 * the DTC memory simply starts empty. */
		UDS_AppDemInit();
		BSW_LateStep = BSW_LATE_STEP_UART;
		break;

	case BSW_LATE_STEP_UART:
		MCAL_UART_Init();
		BSW_LateStep = BSW_LATE_STEP_DONE;
		BSW_stInitDone = 1U;
		g_bswInitDone = 1U;

		/* One-shot task: give the slot back so it can be reused.  The
		 * scheduler popped this task from the ready heap before calling it and
		 * with CycleTime = 0 the tick ISR will not request it again, so
		 * removing ourselves from inside the task is safe. */
		(void)BSW_UnregTask(BSW_LateTaskHandle);
		BSW_LateTaskHandle = BSW_TASK_HANDLE_INVALID;
		break;

	default:
		break;
	}
}

void BSW_InitLateStart(void)
{
	BSW_LateTaskHandle = BSW_RegTask(1U, 254U, BSW_InitLateTask);

	if (BSW_TASK_HANDLE_INVALID == BSW_LateTaskHandle)
	{
		/* No free task slot: run the sequence in place so NVM, UART and the
		 * watchdog still become available (the CAN cadence suffers once). */
		BSW_InitLateBlocking();
	}
}

uint8 BSW_IsInitDone(void)
{
	return BSW_stInitDone;
}

void BSW_InitLateBlocking(void)
{
	/* Fallback when no task slot was left for BSW_InitLateTask(): run the
	 * same sequence in place.  The CAN cadence suffers for a few hundred
	 * milliseconds, but the NVM/UART/watchdog still become available. */
	while (0U == BSW_stInitDone)
	{
		BSW_InitLateTask();
	}
}
