#ifndef _USEROS_H
#define _USEROS_H

/*
 * Cooperative priority scheduler.
 *
 * The ready queue is selected at compile time (BSW_OS_PRIO_QUEUE_ENABLE):
 *
 *   1 (default) binary min-heap priority queue owned by the dispatcher:
 *     - "set ready" is a lock-free publish: an interrupt or a task stores one
 *       byte for its task plus a "something is posted" marker,
 *     - the dispatcher adopts those flags into the heap (O(log N) per task)
 *       and pops the root (highest priority) for each dispatch (O(log N)),
 *     - no interrupt is ever masked by the OS.
 *
 *   0 original linear polling scan:
 *     - the producer sets the task's Request byte,
 *     - the dispatcher scans the whole table on every pass.
 *
 * Both modes use the same table, API and ordering: lower Priority value =
 * higher priority, equal priority runs in handle order.
 *
 * N = MAX_TASK_NUM (default 8, override with -DMAX_TASK_NUM=...): at most
 * ceil(log2(N)) comparisons per heap push/pop.
 *
 * Context rules:
 *   BSW_RegTask / BSW_UnregTask   task context (before BSW_OsStart() or from
 *                                 a task) - heap mode updates the dispatch heap
 *   BSW_RequestTask               task or interrupt context
 */

/*
 * Ready-queue implementation:
 *   1 = heap priority queue (default)
 *   0 = original linear polling scan (smallest code; N loads per dispatch)
 * Can be overridden by the compiler, e.g. -DBSW_OS_PRIO_QUEUE_ENABLE=0.
 */
#ifndef BSW_OS_PRIO_QUEUE_ENABLE
#define BSW_OS_PRIO_QUEUE_ENABLE 0
#endif

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
 * The task is not runnable until it is requested: a periodic task is posted
 * by the tick ISR when its period elapses, an event task (fpCycleTime == 0)
 * is posted by BSW_RequestTask().  Task context only (the ready heap belongs
 * to the dispatcher, so this must not be called from an interrupt).
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
 * The cycle time is set to 0 (the tick ISR stops posting the task), a pending
 * request is removed from the flags and from the ready heap, and the slot
 * becomes free for the next BSW_RegTask().  Task context only.  Safe to call
 * from the task itself, e.g. a one-shot initialisation task that removes
 * itself when done.
 *
 * @param handle Value returned by BSW_RegTask().
 * @return 0 when the task was removed, BSW_TASK_HANDLE_INVALID for an invalid
 *         (or already removed) handle.
 */
unsigned char BSW_UnregTask(unsigned char handle);

/**
 * @brief Make a registered task runnable now.
 *
 * This is the entry point for event tasks (fpCycleTime == 0), which are never
 * posted by the tick ISR.  Periodic tasks can be triggered the same way to run
 * ahead of their period.  The request is two byte stores - no heap access and
 * no interrupt masking - so it is safe from a task and from an interrupt
 * handler (e.g. a CAN RX ISR waking a protocol task).  Asking for a task that
 * is still pending only merges with the pending run, so one task can never
 * appear twice in the ready queue.
 *
 * @param handle Value returned by BSW_RegTask().
 * @return 0 when the task was queued, BSW_TASK_HANDLE_INVALID for an invalid
 *         (or removed) handle.
 */
unsigned char BSW_RequestTask(unsigned char handle);

void BSW_OsStart(void);

unsigned char BSW_GetCpuLoad(void);
void BSW_ResetCpuLoadStat(void);

#endif
