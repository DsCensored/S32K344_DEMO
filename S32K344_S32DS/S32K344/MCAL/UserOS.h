#ifndef _USEROS_H
#define _USEROS_H

/*
 * CPU load statistics:
 *   1 = enable statistics
 *   0 = disable statistics; BSW_GetCpuLoad() always returns 0
 * Can also be overridden by the compiler, e.g. -DBSW_CPU_LOAD_ENABLE=0.
 */
#ifndef BSW_CPU_LOAD_ENABLE
#define BSW_CPU_LOAD_ENABLE 0
#endif

#ifndef BSW_CRYSTAL_COMPENSATION_ENABLE
#define BSW_CRYSTAL_COMPENSATION_ENABLE		(0)
#if BSW_CRYSTAL_COMPENSATION_ENABLE
#define BSW_CRYSTAL_COMPENSATION_CNT		(96000)
#endif
#endif

#define BSW_TASK_HANDLE_INVALID     255U

void BSW_IsrPit0Ch0(void);
unsigned int BSW_GetSystemTick(void);
unsigned int BSW_GetElapsedTime(unsigned int LastTime);

/**
 * @brief Register a task in the first free slot of the scheduler table.
 *
 * @param fpCycleTime Period in system ticks (0 = event task, never requested
 *                    by the tick ISR).
 * @param fpPriority  0..254, lower value = higher priority.
 * @param fpfun       Task function, must not be NULL.
 * @return Task handle (0..MAX_TASK_NUM-1) or BSW_TASK_HANDLE_INVALID (255).
 */
unsigned char BSW_RegTask(unsigned int fpCycleTime, unsigned char fpPriority, void (*fpfun)(void));

/**
 * @brief Remove a task from the scheduler table.
 *
 * The cycle time is set to 0 (the tick ISR stops requesting the task) and the
 * slot becomes free for the next BSW_RegTask().  Safe to call from the task
 * itself, e.g. a one-shot initialisation task that removes itself when done.
 *
 * @param handle Value returned by BSW_RegTask().
 * @return 0 when the task was removed, BSW_TASK_HANDLE_INVALID for an invalid
 *         (or already removed) handle.
 */
unsigned char BSW_UnregTask(unsigned char handle);

void BSW_OsStart(void);

unsigned char BSW_GetCpuLoad(void);
void BSW_ResetCpuLoadStat(void);

#endif
