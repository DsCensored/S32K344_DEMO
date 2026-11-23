/*
 * UserOS.c
 *
 *  Created on: 2026年10月20日
 *      Author: CENSORED
 *
 *  v6: the ready-queue implementation is a compile-time option
 *      (BSW_OS_PRIO_QUEUE_ENABLE in UserOS.h):
 *
 *        1 (default) heap priority queue:
 *          - "set ready" from an ISR/task is a lock-free publish: one byte
 *            for the task plus a "something is posted" marker,
 *          - the dispatcher (thread mode) adopts the flags into a binary
 *            min-heap and pops the highest priority in O(log N),
 *          - no critical section at all.
 *
 *        0 original linear polling scan:
 *          - the tick ISR / BSW_RequestTask() set the task's Request byte,
 *          - the dispatcher scans all MAX_TASK_NUM slots for the highest
 *            priority and clears the selected Request (v3 behaviour).
 *
 *      Both modes share the task table, the API, the priority/tie-break rule
 *      and therefore the dispatch order; only the ready bookkeeping differs.
 *
 *  v5: (heap mode) the heap is owned by the dispatcher, never touched by an
 *      interrupt; the v4 implementation masked interrupts around every push
 *      and pop, which delayed interrupts for no reason.
 *
 *  v3: CPU load statistics, overflow window aligned with OS_tiSystemTick
 *      (~49.7 days at 1 ms)
 *      - no extra ISR statements
 *      - no 64-bit divide, no 64-bit multiply
 *      - shift normalisation keeps total at up to 2^32-1 ticks
 */
#include <stdlib.h>
#include "UserOS.h"

#ifndef MAX_TASK_NUM
#define MAX_TASK_NUM 8
#endif

/*
 * Task handles are 8-bit (255 is BSW_TASK_HANDLE_INVALID), and OS_heapPos
 * stores "heap index + 1", so the table can hold at most 255 tasks.
 */
#if (MAX_TASK_NUM > 255)
#error "MAX_TASK_NUM is limited to 255 by the 8-bit handles"
#endif

typedef struct {
	unsigned int CycleTime;
	unsigned int LastTime;
	unsigned char Priority;
#if (BSW_OS_PRIO_QUEUE_ENABLE == 0)
	volatile unsigned char Request;   /* polling mode: run me */
#endif
	void (*fun)(void);
}OS_sch;

/* Zero-initialised: a free slot is "fun == NULL && CycleTime == 0", so an
 * event task (CycleTime == 0, fun != NULL) still owns its slot.  Scanning for
 * a free slot instead of keeping a counter means a slot released by
 * BSW_UnregTask() is reused by the next BSW_RegTask(). */
static OS_sch OS_sch_buf[MAX_TASK_NUM];

static volatile unsigned int OS_tiSystemTick = 0;

/* First slot that is not used by a task, BSW_TASK_HANDLE_INVALID if none. */
static unsigned char OS_FindFreeTaskSlot(void)
{
	unsigned char i;

	for (i = 0U; i < MAX_TASK_NUM; i++)
	{
		if ((NULL == OS_sch_buf[i].fun) && (0U == OS_sch_buf[i].CycleTime))
		{
			return i;
		}
	}

	return BSW_TASK_HANDLE_INVALID;
}

/* ====================================================================== */
/*                     Ready queue: heap priority queue                   */
/* ====================================================================== */
#if (BSW_OS_PRIO_QUEUE_ENABLE != 0)

/*
 *  Producer side ("set"), task or interrupt context, no masking:
 *    OS_ReadyFlag[handle] = 1;   one atomic byte store per request
 *    OS_ReadyAny          = 1;   "something was posted" marker
 *
 *  Consumer side, thread mode only:
 *    clear OS_ReadyAny, adopt every set flag into the heap, pop the root.
 *    OS_heapPos[] dedupes, so one task is queued at most once.
 *
 *  The flag is stored before the marker and the dispatcher clears the marker
 *  before scanning, so a request is either seen by that scan or leaves the
 *  marker set for the next one.  No request can be lost.
 */
static volatile unsigned char OS_ReadyFlag[MAX_TASK_NUM];
static volatile unsigned char OS_ReadyAny;

/*
 *  OS_heap[0 .. OS_heapSize-1]  task handles, root = next task to run
 *  OS_heapPos[h]                 heap index of handle h, plus one
 *                                (0 = handle h is not in the heap, so the
 *                                 zero-initialised array is already valid)
 *
 *  compare: lower Priority wins; equal Priority -> lower handle wins.
 *  A handle is present at most once, so OS_heapSize <= MAX_TASK_NUM always.
 *  Written by the dispatcher and by BSW_UnregTask(), i.e. thread mode only.
 */
static unsigned char OS_heap[MAX_TASK_NUM];
static unsigned char OS_heapPos[MAX_TASK_NUM];
static unsigned char OS_heapSize;

/* True when handle a has to be dispatched before handle b. */
static unsigned char OS_HeapLess(unsigned char a, unsigned char b)
{
	unsigned char prioA = OS_sch_buf[a].Priority;
	unsigned char prioB = OS_sch_buf[b].Priority;

	if (prioA != prioB)
	{
		return (unsigned char)(prioA < prioB);
	}

	return (unsigned char)(a < b);
}

static void OS_HeapSwap(unsigned char a, unsigned char b)
{
	unsigned char tmp = OS_heap[a];

	OS_heap[a] = OS_heap[b];
	OS_heap[b] = tmp;

	OS_heapPos[OS_heap[a]] = (unsigned char)(a + 1U);
	OS_heapPos[OS_heap[b]] = (unsigned char)(b + 1U);
}

static void OS_HeapSiftUp(unsigned char pos)
{
	while (pos > 0U)
	{
		unsigned char parent = (unsigned char)((pos - 1U) >> 1);

		if (0U == OS_HeapLess(OS_heap[pos], OS_heap[parent]))
		{
			return;
		}

		OS_HeapSwap(pos, parent);
		pos = parent;
	}
}

static void OS_HeapSiftDown(unsigned char pos)
{
	for (;;)
	{
		/* 32-bit child indices: with 8-bit arithmetic, (254 << 1) + 1 would
		 * wrap and make a leaf compare against the root. */
		unsigned int  left  = ((unsigned int)pos << 1) + 1U;
		unsigned int  right = left + 1U;
		unsigned char best  = pos;

		if ((left < (unsigned int)OS_heapSize) && (0U != OS_HeapLess(OS_heap[left], OS_heap[best])))
		{
			best = (unsigned char)left;
		}
		if ((right < (unsigned int)OS_heapSize) && (0U != OS_HeapLess(OS_heap[right], OS_heap[best])))
		{
			best = (unsigned char)right;
		}
		if (best == pos)
		{
			return;
		}

		OS_HeapSwap(pos, best);
		pos = best;
	}
}

/* Remove the heap element at pos; keeps the heap invariant for the element
 * that is moved into the hole (it may have to travel up or down). */
static void OS_HeapRemoveAt(unsigned char pos)
{
	unsigned char last  = (unsigned char)(OS_heapSize - 1U);
	unsigned char moved;

	OS_heapSize = last;
	OS_heapPos[OS_heap[pos]] = 0U;

	if (pos == last)
	{
		return;
	}

	moved = OS_heap[last];
	OS_heap[pos] = moved;
	OS_heapPos[moved] = (unsigned char)(pos + 1U);

	OS_HeapSiftUp(pos);
	OS_HeapSiftDown((unsigned char)(OS_heapPos[moved] - 1U));
}

/* Queue a handle in the private ready heap.  Thread context only: no
 * interrupt touches the heap, so there is nothing to mask. */
static void OS_HeapPush(unsigned char handle)
{
	unsigned char pos;

	if (0U != OS_heapPos[handle])
	{
		return;                        /* already queued */
	}

	/* Cannot overflow: one entry per handle, and handles are < MAX_TASK_NUM. */
	pos = OS_heapSize;
	OS_heap[pos] = handle;
	OS_heapPos[handle] = (unsigned char)(pos + 1U);
	OS_heapSize = (unsigned char)(pos + 1U);

	OS_HeapSiftUp(pos);
}

/* Publish a request.  Task or interrupt context, no interrupt masking.
 *
 * The flag is written before the marker; the dispatcher clears the marker
 * before it scans.  So a request either happens before that scan (and the
 * scan sees the flag) or after the marker clear (and the marker is set again
 * by this store, forcing another scan). */
static void OS_TaskPost(unsigned char handle)
{
	OS_ReadyFlag[handle] = 1U;
	OS_ReadyAny = 1U;
}

/* Adopt every posted flag into the ready heap (thread mode).  A flag is
 * cleared before the handle is queued; a request that arrives after the clear
 * simply runs the task again, as in the old one-pending-run model. */
static void OS_TaskAdopt(void)
{
	unsigned char i;

	OS_ReadyAny = 0U;                  /* clear first, then scan */

	for (i = 0U; i < MAX_TASK_NUM; i++)
	{
		if (0U != OS_ReadyFlag[i])
		{
			OS_ReadyFlag[i] = 0U;

			/* BSW_UnregTask() may have freed the slot between the post and
			 * this adopt; never queue a handle whose function is gone. */
			if (NULL != OS_sch_buf[i].fun)
			{
				OS_HeapPush(i);
			}
		}
	}
}

/* Highest-priority runnable task, BSW_TASK_HANDLE_INVALID when idle. */
static unsigned char OS_TaskTake(void)
{
	unsigned char handle;

	if (0U != OS_ReadyAny)
	{
		OS_TaskAdopt();
	}

	if (0U == OS_heapSize)
	{
		return BSW_TASK_HANDLE_INVALID;
	}

	handle = OS_heap[0];
	OS_HeapRemoveAt(0U);

	return handle;
}

/* Drop any pending run of handle (called by BSW_UnregTask). */
static void OS_TaskCancel(unsigned char handle)
{
	OS_ReadyFlag[handle] = 0U;

	if (0U != OS_heapPos[handle])
	{
		OS_HeapRemoveAt((unsigned char)(OS_heapPos[handle] - 1U));
	}
}

/* Forget a pending run left over on a slot that is being registered. */
static void OS_TaskReset(unsigned char handle)
{
	OS_ReadyFlag[handle] = 0U;
}

/* ====================================================================== */
/*                  Ready queue: original linear polling scan             */
/* ====================================================================== */
#else

/* Original behaviour: the producer only sets Request, the dispatcher scans
 * the whole table on every pass.  No heap and no critical section, but the
 * idle loop costs MAX_TASK_NUM loads and every dispatch costs a full scan. */
static void OS_TaskPost(unsigned char handle)
{
	OS_sch_buf[handle].Request = 1U;
}

/* Highest-priority runnable task, BSW_TASK_HANDLE_INVALID when idle.
 * Ascending scan with a strict "lower priority wins" compare: equal priority
 * keeps the lower handle, exactly like the heap comparator. */
static unsigned char OS_TaskTake(void)
{
	unsigned char handle = BSW_TASK_HANDLE_INVALID;
	unsigned char best   = 255U;
	unsigned char i;

	for (i = 0U; i < MAX_TASK_NUM; i++)
	{
		if (0U != OS_sch_buf[i].Request)
		{
			if (NULL == OS_sch_buf[i].fun)
			{
				/* Stale request for a slot that was just unregistered. */
				OS_sch_buf[i].Request = 0U;
			}
			else if (OS_sch_buf[i].Priority < best)
			{
				best   = OS_sch_buf[i].Priority;
				handle = i;
			}
		}
	}

	if (BSW_TASK_HANDLE_INVALID != handle)
	{
		OS_sch_buf[handle].Request = 0U;
	}

	return handle;
}

/* Drop any pending run of handle (called by BSW_UnregTask). */
static void OS_TaskCancel(unsigned char handle)
{
	OS_sch_buf[handle].Request = 0U;
}

/* Forget a pending run left over on a slot that is being registered. */
static void OS_TaskReset(unsigned char handle)
{
	OS_sch_buf[handle].Request = 0U;
}

#endif /* BSW_OS_PRIO_QUEUE_ENABLE */

/* ==================== CPU 负载率统计 ==================== */
/*
 * 设计说明：
 *   1. ISR 里无任何新增语句，只保留原有 OS_tiSystemTick++；
 *   2. 空闲时长由调度器在"进入空闲 / 退出空闲"边沿累加，
 *      事件驱动、不漏采短任务；
 *   3. 负载率在获取侧用 32 位乘除计算，
 *      大 total 时通过同步右移 busy/total 归一化，
 *      使 32 位乘 100 不溢出，精度损失 < 0.4%；
 *   4. 统计窗口上限 = OS_tiSystemTick 的溢出周期
 *      = 2^32 tick（1 ms tick 下约 49.7 天），与系统 tick 同量级。
 */
#if (BSW_CPU_LOAD_ENABLE == 1)
static volatile unsigned int OS_tiIdleTicks = 0;   /* 累计空闲 tick 数     */
static volatile unsigned int OS_tiResetMark = 0;   /* 上次复位时的系统 tick */
#endif
/* ====================================================== */

/*
* Name:BSW_IsrPit0Ch0
* Formal para:NULL
* return value:NULL
* description:GPT PIT0 CH0 ISR Notice
*/
void BSW_IsrPit0Ch0(void)
{
	unsigned int now;

#if BSW_CRYSTAL_COMPENSATION_ENABLE
	static unsigned int COC_Cnt = BSW_CRYSTAL_COMPENSATION_CNT;

	if(--COC_Cnt == 0)
	{
		COC_Cnt = BSW_CRYSTAL_COMPENSATION_CNT;
		OS_tiSystemTick++;
	}
#endif

	now = ++OS_tiSystemTick;

	for (unsigned char i = 0; i < MAX_TASK_NUM; i++)
	{
		unsigned int cycle = OS_sch_buf[i].CycleTime;

		/* CycleTime == 0 marks a free slot or an event task: never requested
		 * by the tick.  Checked first so free slots cost one compare. */
		if ((0U != cycle) && ((now - OS_sch_buf[i].LastTime) >= cycle))
		{
			OS_sch_buf[i].LastTime += cycle;
			OS_TaskPost(i);            /* heap: two bytes; polling: one byte */
		}
	}
}

/*
* Name:BSW_RegTask
* Formal para:fpCycleTime, fpPriority(0~254), fpfun; when cycletime eql 0, this task is a event
* return value:task handler(0~MAX_TASK_NUM-1), 255 means failure
* description:register a task in the first free slot.  Task context: in heap
*             mode the dispatch heap is updated from here, so this must not
*             run from an ISR.
*/
unsigned char BSW_RegTask(unsigned int fpCycleTime, unsigned char fpPriority, void (*fpfun)(void))
{
	unsigned char handle;

	if ((NULL == fpfun) || (BSW_TASK_HANDLE_INVALID == fpPriority))
	{
		return BSW_TASK_HANDLE_INVALID;
	}

	handle = OS_FindFreeTaskSlot();
	if (BSW_TASK_HANDLE_INVALID != handle)
	{
		/* Handler and priority first, CycleTime last: the tick ISR only starts
		 * posting the task once the slot is completely filled in.  Drop a
		 * request left over from the previous owner of the slot. */
		OS_sch_buf[handle].fun       = fpfun;
		OS_sch_buf[handle].Priority  = fpPriority;
		OS_sch_buf[handle].LastTime  = OS_tiSystemTick;
		OS_TaskReset(handle);
		OS_sch_buf[handle].CycleTime = fpCycleTime;
	}

	return handle;
}

/*
* Name:BSW_UnregTask
* Formal para:handle (value returned by BSW_RegTask)
* return value:0 = task removed, 255 means invalid handle
* description:remove a task from the scheduler: its cycle time is set to 0 so
*             the tick ISR never posts it again, a pending request is removed
*             (heap entry in heap mode, Request byte in polling mode), and the
*             slot becomes free for the next BSW_RegTask.  Task context only.
*/
unsigned char BSW_UnregTask(unsigned char handle)
{
	if ((handle >= MAX_TASK_NUM) || (NULL == OS_sch_buf[handle].fun))
	{
		return BSW_TASK_HANDLE_INVALID;
	}

	/* Deactivate first (the ISR checks CycleTime), then drop a pending
	 * request, then the handler so a queued entry can never call NULL. */
	OS_sch_buf[handle].CycleTime = 0U;
	OS_TaskCancel(handle);
	OS_sch_buf[handle].fun       = NULL;
	OS_sch_buf[handle].LastTime  = 0U;
	OS_sch_buf[handle].Priority  = 255U;

	return 0U;
}

/*
* Name:BSW_RequestTask
* Formal para:handle (value returned by BSW_RegTask)
* return value:0 = task queued, 255 means invalid handle
* description:make a registered task runnable now.  Event tasks (CycleTime 0)
*             only ever run through this call; periodic tasks can also be
*             triggered from an ISR without waiting for the next period.
*             Calling it again while the task is already pending is a no-op.
*             Safe from task and interrupt context (heap mode: two byte
*             stores, polling mode: one byte store).
*/
unsigned char BSW_RequestTask(unsigned char handle)
{
	if ((handle >= MAX_TASK_NUM) || (NULL == OS_sch_buf[handle].fun))
	{
		return BSW_TASK_HANDLE_INVALID;
	}

	OS_TaskPost(handle);

	return 0U;
}

/*
* Name:BSW_GetSystemTick
* Formal para:NULL
* return value:current system tick
* description:get current system tick
*/
unsigned int BSW_GetSystemTick(void)
{
	return OS_tiSystemTick;
}

/*
* Name:BSW_GetPassTime
* Formal para:LastTime
* return value:elapsed time since LastTime
* description:get the elapsed time since LastTime
*/
unsigned int BSW_GetElapsedTime(unsigned int LastTime)
{
	return OS_tiSystemTick - LastTime;
}

/*
* Name:BSW_OsStart
* Formal para:NULL
* return value:NULL
* description:start the OS scheduler: run the highest-priority ready task,
*             forever.  Heap mode adopts posted flags and pops the heap;
*             polling mode scans the table.  Neither masks interrupts.
*/
void BSW_OsStart(void)
{
#if (BSW_CPU_LOAD_ENABLE == 1)
	/* ---- 负载统计：以启动时刻为统计窗口起点 ---- */
	unsigned int  idleStart = 0;
	unsigned char wasIdle   = 0;

	OS_tiIdleTicks = 0;
	OS_tiResetMark = OS_tiSystemTick;
	idleStart      = OS_tiSystemTick;
	/* -------------------------------------------- */
#endif

	for (;;)
	{
		unsigned char handle = OS_TaskTake();

		if (BSW_TASK_HANDLE_INVALID != handle)
		{
#if (BSW_CPU_LOAD_ENABLE == 1)
			/* 刚退出空闲：结算上一段空闲时长 */
			if (wasIdle)
			{
				OS_tiIdleTicks += OS_tiSystemTick - idleStart;
				wasIdle = 0;
			}
#endif

			OS_sch_buf[handle].fun();
		}
#if (BSW_CPU_LOAD_ENABLE == 1)
		else
		{
			/* 刚进入空闲：记录空闲起点 */
			if (!wasIdle)
			{
				idleStart = OS_tiSystemTick;
				wasIdle   = 1;
			}
		}
#endif
	}
}

/*
* Name:BSW_GetCpuLoad
* Formal para:NULL
* return value:0~100，CPU 负载率百分比
* description:获取自上次 BSW_ResetCpuLoadStat() 以来的平均负载率
*
* 计算式：load = busy * 100 / total
*   其中 total = OS_tiSystemTick - OS_tiResetMark（无符号差，回绕正确）
*        busy  = total - idle
*
* 溢出处理：
*   直接算 busy*100 时，若 total > 2^32/100 ≈ 42,949,672（1 ms 下约 11.9 h），
*   则 busy*100 可能溢出 32 位。本函数对 total 和 busy 同步右移，
*   直到 total < 2^32/100，此时 busy*100 必不溢出。
*
*   最大统计窗口 = 2^32 tick（1 ms 下约 49.7 天），与 OS_tiSystemTick
*   的溢出周期同量级，即负载统计的窗口上限只受系统 tick 本身的约束。
*
*   精度损失：每次右移 busy/total 各丢 1 位；
*   最坏情况 total ≈ 2^32 时需右移 8 次，
*   相对误差 < 100 / 2^8 ≈ 0.4%（绝对误差 < 0.4 个百分点），
*   对"判断 < 50%"这类阈值判断完全足够。
*/
unsigned char BSW_GetCpuLoad(void)
{
#if (BSW_CPU_LOAD_ENABLE == 1)
	unsigned int total = OS_tiSystemTick - OS_tiResetMark;
	unsigned int idle  = OS_tiIdleTicks;
	unsigned int busy;

	if (total == 0U)
	{
		return 0;
	}
	if (idle > total)          /* 竞态保护：读 idle 时若恰逢结算，最多差 1 tick */
	{
		idle = total;
	}
	busy = total - idle;

	/*
	 * 移位归一化：保证 busy * 100 不溢出 32 位
	 * 最多 8 次移位（2^32 → 2^24，2^24 < 2^32/100 ≈ 2^25.36）
	 */
	while (total >= (0xFFFFFFFFU / 100U))
	{
		total >>= 1;
		busy  >>= 1;
		if (total == 0U)       /* 极端保护，理论上不会进入 */
		{
			return 0;
		}
	}

	return (unsigned char)((busy * 100U) / total);
#else
	return 0U;
#endif
}

/*
* Name:BSW_ResetCpuLoadStat
* Formal para:NULL
* return value:NULL
* description:清零负载统计，以当前时刻为新的统计窗口起点
*
* 注意：本函数应在调度器执行的任务中调用（即 wasIdle == 0 的时刻），
*       这样调度器内部的 idleStart 无需额外同步，逻辑自然一致。
*
* 统计窗口必须 < 2^32 tick（1 ms 下约 49.7 天），否则 total 的无符号差
* 会因 OS_tiSystemTick 回绕而失真——这与系统 tick 自身的回绕约束一致。
*/
void BSW_ResetCpuLoadStat(void)
{
#if (BSW_CPU_LOAD_ENABLE == 1)
	OS_tiIdleTicks = 0;
	OS_tiResetMark = OS_tiSystemTick;
#endif
}
