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
#include "Nvm.h"
#include "NvmApp.h"
#include "RTE.h"
#include "AutoSarNm.h"
#include "S32K344_SWT.h"
#include "Dio.h"
#include "Dio_Cfg.h"
#include "Wdg_43_Instance0.h"
#include "ASP.h"
#include "DSC.h"
#include "BSW_Init.h"      /* two-phase start-up                            */
#include "BSW_WDG.h"       /* guarded watchdog service                     */
#include "hse_trng.h"      /* deferred HSE TRNG self-test                   */

/* -------------------------------------------------------------------------
 * Simple NVM demo: keep a power-up counter in NVM block 3.
 * Set to 0 to disable the extra Fee write at start-up.
 * ---------------------------------------------------------------------- */
#define ASW_NVM_DEMO_ENABLE     0

#if (ASW_NVM_DEMO_ENABLE == 1)
static void ASW_NvmBootCounter(void)
{
	uint8 bootCount = 0U;

	/* byte access on the enum-selected block */
	if (E_OK != Nvm_ReadByte(NVM_BLOCK_3, 0U, &bootCount))
	{
		bootCount = 0U;
	}

	bootCount++;

	(void)Nvm_WriteByte(NVM_BLOCK_3, 0U, bootCount);
	(void)Nvm_WriteByte(NVM_BLOCK_3, 1U, (uint8)(bootCount ^ 0xFFU));

	/* make the RAM changes permanent in the DFlash */
	(void)Nvm_CommitBlock(NVM_BLOCK_3);
}
#endif

static void ASW_userTask2ms(void)
{
	Can_43_FLEXCAN_MainFunction_Read();
	Can_43_FLEXCAN_MainFunction_Write();
	Can_43_FLEXCAN_MainFunction_Mode();

	/* BSW_UART6_RxPoll() refuses to touch the Uart driver until the deferred
	 * start-up has initialised UART6/DMA (BSW_UART.c guard). */
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
	RTE_POLLING();
	/* AUTOSAR NM uses 10 ms ticks (AUROSAR_NM_MODEL_RUN_STEP).  Run it after
	 * RTE_POLLING() so BSW_stKL15 reflects the current ignition state. */
	AutoSarNm_step();
	BSW_CANRX_TIMEOUT_POLL();
	Nvm_UserBlock_Polling_10ms();
}
static void ASW_userTask20ms(void)
{
	ASP_step();
}
static void ASW_userTask50ms(void)
{
	DSC_step();
}
static void ASW_userTask100ms(void)
{
	UDS_DCM_Poll();
}
static void ASW_userTask1000ms(void)
{
	static unsigned char trngSelfTestDone = 0U;

	/* Watchdog: BSW_WDG_Service() is a counted no-op until the deferred
	 * start-up has initialised the Wdg driver, so no gate is needed here. */
	BSW_WDG_Service();

	unsigned char data[] = "Hallo world";
	(void)BSW_UART6_Send(data,sizeof(data));

	/* HSE TRNG self-test: was part of the blocking start-up.  It is a pure
	 * diagnostic (result in g_hseTrngSelfTest[]), so it runs once a second
	 * after power-up instead of delaying the CAN start.  Each request can take
	 * a few ms; this one-off jitter is acceptable after the start-up phase. */
#if (HSE_TRNG_SELFTEST_ON_BOOT != 0U)
	if (0U == trngSelfTestDone)
	{
		trngSelfTestDone = 1U;
		(void)HseTrng_SelfTest();
	}
#endif

//	ASW_stLEDR1Ctrl = !ASW_stLEDR1Ctrl;
//	ASW_stLEDG1Ctrl = !ASW_stLEDG1Ctrl;
//	ASW_stLEDB1Ctrl = !ASW_stLEDB1Ctrl;
//	ASW_stLEDY1Ctrl = !ASW_stLEDY1Ctrl;

}

void ASW_Init(void)
{
	BSW_CANRX_INIT(CAN_CHN1_RX, (unsigned char)CAN_CHN1_RX_COUNT);
	UDS_CanTpInit();
	UDS_DCM_Init();
	ASW_CanInit();

	/* Pure data initialisation of the NM model.  It has to run before
	 * Icu_Init() (which reads the wake-up reason), so it stays here and not in
	 * the deferred start-up. */
	AutoSarNm_initialize();

	ASP_initialize();
	DSC_initialize();

	BSW_RegTask(2, 0, ASW_userTask2ms);
	BSW_RegTask(5, 1, ASW_userTask5ms);
	BSW_RegTask(10, 2, ASW_userTask10ms);
	BSW_RegTask(20, 2, ASW_userTask20ms);
	BSW_RegTask(50, 3, ASW_userTask50ms);
	BSW_RegTask(100, 3, ASW_userTask100ms);
	BSW_RegTask(1000, 4, ASW_userTask1000ms);

	/* Deferred start-up (watchdog, ICU, NVM, UART) at the lowest priority:
	 * it runs only when no CAN/diagnostic task is pending, advances one slice
	 * per millisecond and unregisters itself when it is done, so the periodic
	 * frames keep their timing and the task slot is reused afterwards. */
	BSW_InitLateStart();

#if (ASW_NVM_DEMO_ENABLE == 1)
	ASW_NvmBootCounter();
#endif
}

