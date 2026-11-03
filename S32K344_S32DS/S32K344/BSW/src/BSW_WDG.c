/*
 * BSW_WDG.c
 *
 *  Created on: 2026年11月3日
 *      Author: CENSORED
 */
#include "Mcu.h"

void Wdg_CallBackNotification0(void)
{
	Mcu_PerformReset();
}
