/*
 * BSW_WDG.c
 *
 *  Created on: 2026年11月3日
 *      Author: CENSORED
 *
 *  Watchdog service of the application.
 *
 *  Wdg_43_Instance0 belongs to the deferred start-up (BSW_InitLateTask), so it
 *  does not exist during the first milliseconds after reset, while the first
 *  CAN frames are already on the bus.  This wrapper is the only place the
 *  application should use: it refuses to touch the Wdg driver until
 *  BSW_WDG_Init() has run, so a task that runs too early can neither drive an
 *  uninitialised driver nor silently skip the feed.
 */
#include "BSW_WDG.h"

#include "Mcu.h"
#include "Wdg_43_Instance0.h"
#include "Wdg_CfgExt.h"
#include "WdgIf_Types.h"

/* TRUE once Wdg_43_Instance0_Init() has been executed. */
static volatile boolean BSW_flgWdgReady = FALSE;

/* Debug counter: services refused because the driver is not initialised. */
volatile uint32 g_wdgNotReadyRejects = 0U;

void BSW_WDG_Init(void)
{
	/* Same sequence the former MCAL_WDG_Init() used. */
	Wdg_43_Instance0_Init(&Wdg_Config_0);
	Wdg_43_Instance0_SetMode(WDGIF_SLOW_MODE);

	BSW_flgWdgReady = TRUE;
}

boolean BSW_WDG_IsReady(void)
{
	return BSW_flgWdgReady;
}

void BSW_WDG_Service(void)
{
	if (FALSE == BSW_flgWdgReady)
	{
		/* Not initialised yet: the driver must not be touched. */
		g_wdgNotReadyRejects++;
		return;
	}

	Wdg_43_Instance0_Service();
}

void Wdg_CallBackNotification0(void)
{
	Mcu_PerformReset();
}
