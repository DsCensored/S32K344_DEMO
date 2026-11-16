/*
 * BSW_WDG.h
 *
 *  Created on: 2026年11月3日
 *      Author: CENSORED
 *
 *  Watchdog service of the application (see BSW_WDG.c).
 *
 *  The Wdg driver is initialised in the deferred start-up (BSW_InitLateTask),
 *  after the OS has already started.  These wrappers intercept every access
 *  before that, so cyclic tasks do not need their own "initialised" check.
 */

#ifndef INCLUDE_BSW_WDG_H_
#define INCLUDE_BSW_WDG_H_

#include "Std_Types.h"

/**
 * @brief Initialise the watchdog driver (Wdg_43_Instance0, slow mode).
 *
 * Call it from the deferred start-up; until then BSW_WDG_Service() is a
 * counted no-op.
 */
void BSW_WDG_Init(void);

/** TRUE once BSW_WDG_Init() has run. */
boolean BSW_WDG_IsReady(void);

/**
 * @brief Feed the watchdog.
 *
 * Does nothing (and increments g_wdgNotReadyRejects) while the driver is not
 * initialised, so it can be called unconditionally from a cyclic task.
 */
void BSW_WDG_Service(void);

/** Number of services refused because the driver was not initialised yet. */
extern volatile uint32 g_wdgNotReadyRejects;

#endif /* INCLUDE_BSW_WDG_H_ */
