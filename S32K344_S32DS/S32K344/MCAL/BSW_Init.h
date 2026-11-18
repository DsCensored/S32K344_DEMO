/*
 * BSW_Init.h
 *
 *  Created on: 2026年10月20日
 *      Author: CENSORED
 *
 *  Two-phase start-up of the application.
 *
 *  The first CAN frame has to leave the ECU as early as possible (target:
 *  < 150 ms after power-on), but the complete MCAL start-up takes much longer:
 *  the NVM stack alone spends ~50 ms in the Fee DFlash scan, and UART/DMA, the
 *  ICU and the watchdog add their own share.
 *
 *  Therefore the initialisation is split:
 *
 *    BSW_InitEarly()   MCU clock, Port, Platform (interrupts), GPT (1 ms tick)
 *                      and CAN/CanIf.  Everything the first frame needs.
 *                      Called from main() before the OS is started.
 *
 *    BSW_InitLateTask() the rest (NVM, ICU, watchdog, UART/DMA).  Registered
 *                      as a 1 ms low-priority task; it advances one small step
 *                      per call so the cooperative scheduler keeps dispatching
 *                      the 2 ms / 5 ms CAN tasks and the periodic frames are
 *                      not delayed (NVM init is sliced by Nvm_InitStep()).
 *
 *  Tasks that need the late initialisation must check BSW_IsInitDone().
 */

#ifndef BSW_INIT_H
#define BSW_INIT_H

#include "Std_Types.h"

/**
 * @brief Initialise the MCU clock, Port, Platform, GPT (1 ms tick) and CAN.
 *
 * After this call the CAN controller is started and BSW_GetSystemTick() runs,
 * i.e. BSW_CANTX_POLL() may be used to send the first periodic frame.
 */
void BSW_InitEarly(void);

/**
 * @brief One step of the deferred initialisation (1 ms, low priority task).
 *
 * Sequence: watchdog -> ICU -> NVM (sliced) -> UART/DMA.
 * The NVM part only advances a small slice per call, so a single call stays
 * far below 1 ms and never disturbs the CAN cadence.  When the sequence is
 * complete the task removes itself from the scheduler (BSW_UnregTask).
 */
void BSW_InitLateTask(void);

/**
 * @brief Register BSW_InitLateTask() as a 1 ms low-priority task.
 *
 * Called once from ASW_Init().  If no task slot is free the sequence is
 * executed in place instead (BSW_InitLateBlocking()).
 */
void BSW_InitLateStart(void);

/**
 * @brief TRUE once the deferred initialisation has finished.
 *
 * Until then the UART, the NVM shadow, the ICU and the watchdog are not
 * available; tasks that use them must skip their work.
 */
uint8 BSW_IsInitDone(void);

/* Debug view of the deferred start-up (see BSW_Init.c):
 *   g_bswInitLateStep : step currently executed
 *                       (0 WDG, 1 ICU, 2 NVM, 3 UART, 4 done)
 *   g_bswInitLateNvm  : 1 once the NVM start-up reported DONE
 *   g_bswInitDone     : 1 once the whole sequence finished
 * Watching these in the debugger shows whether e.g. the NVM initialisation
 * really ran; a step that never changes means the sequence is stuck there. */
extern volatile uint8 g_bswInitLateStep;
extern volatile uint8 g_bswInitLateNvm;
extern volatile uint8 g_bswInitDone;

/**
 * @brief Run the deferred initialisation in place (blocking).
 *
 * Only for the case where BSW_InitLateTask() could not be registered as a
 * task.  Blocks for several hundred milliseconds (NVM scan), which delays the
 * periodic CAN frames - do not use it in the normal start-up.
 */
void BSW_InitLateBlocking(void);

#endif /* BSW_INIT_H */
