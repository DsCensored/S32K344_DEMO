/*
 * BSW_Init.h
 *
 *  Created on: 2026年10月20日
 *      Author: CENSORED
 *
 *  Bring-up of the MCAL/Boot basic software.
 *
 *  BSW_Init() runs the complete initialisation; the two "early" entry points
 *  exist because the APP integrity check of Boot_TryJumpToApp() runs *before*
 *  BSW_Init() (the fast path must decide about the jump without starting CAN,
 *  GPT, ...), but needs the system clock (HSE / DFlash timing) and the NVM
 *  stack (MemAcc -> Fee -> Mem_43_INFLS).
 *
 *  Both early entry points are idempotent: whatever they already initialised
 *  is not initialised again by BSW_Init(), so a check that fails and lets the
 *  normal boot continue does not bring the memory stack up twice.
 */

#ifndef BSW_INIT_H
#define BSW_INIT_H

#include "Std_Types.h"

/**
 * @brief Initialise the MCU clocks (PLL -> 160 MHz) once.
 *
 * May be called at any time after reset; every later call is a no-op.
 *
 * @return E_OK
 */
Std_ReturnType BSW_EarlyInitClock(void);

/**
 * @brief Initialise the NVM stack (Nvm / Fee / MemAcc / Mem_43_INFLS) once.
 *
 * The RAM shadow of the four NVM blocks is loaded from the DFlash, so
 * Nvm_BlockX_Read_U8() can be used afterwards.
 *
 * @return E_OK when the module is initialised, E_NOT_OK when the memory stack
 *         failed (the blocks then read as 0xFF, i.e. "not programmed").
 */
Std_ReturnType BSW_EarlyInitNvm(void);

/**
 * @brief Complete BSW initialisation (MCU clock, Port, Platform, GPT, CAN, NVM).
 */
void BSW_Init(void);

#endif /* BSW_INIT_H */
