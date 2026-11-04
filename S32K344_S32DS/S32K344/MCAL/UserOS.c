/*
 * UserOS.c
 *
 *  Created on: 2026年10月20日
 *      Author: CENSORED
 *
 *  v3：CPU 负载率统计，溢出窗口对齐 OS_tiSystemTick（约 49.7 天 @ 1ms）
 *      - ISR 零新增
 *      - 无 64 位除法，无 64 位乘法
 *      - 通过移位归一化，使 total 可达 2^32-1 tick
 */
#include <stdlib.h>
#include "UserOS.h"

#define MAX_TASK_NUM 8

typedef struct {
	unsigned int CycleTime;
	unsigned int LastTime;
	unsigned char Priority;
	volatile unsigned char Request;
	void (*fun)(void);
}OS_sch;

static OS_sch OS_sch_buf[MAX_TASK_NUM] =
{
	//Task1
	{
		.CycleTime = 10,
		.LastTime = 0,
		.Priority = 255,
		.Request = 0,
		.fun = NULL
	},
	//Task2
	{
		.CycleTime = 10,
		.LastTime = 0,
		.Priority = 255,
		.Request = 0,
		.fun = NULL
	},
	//Task3
	{
		.CycleTime = 10,
		.LastTime = 0,
		.Priority = 255,
		.Request = 0,
		.fun = NULL
	},
	//Task4
	{
		.CycleTime = 10,
		.LastTime = 0,
		.Priority = 255,
		.Request = 0,
		.fun = NULL
	},
	//Task5
	{
		.CycleTime = 10,
		.LastTime = 0,
		.Priority = 255,
		.Request = 0,
		.fun = NULL
	},
	//Task6
	{
		.CycleTime = 10,
		.LastTime = 0,
		.Priority = 255,
		.Request = 0,
		.fun = NULL
	},
	//Task7
	{
		.CycleTime = 10,
		.LastTime = 0,
		.Priority = 255,
		.Request = 0,
		.fun = NULL
	},
	//Task8
	{
		.CycleTime = 10,
		.LastTime = 0,
		.Priority = 255,
		.Request = 0,
		.fun = NULL
	}
};

static unsigned char OS_numTolTaskNum = 0;
static volatile unsigned int OS_tiSystemTick = 0;

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
#if BSW_CRYSTAL_COMPENSATION_ENABLE
	static unsigned int COC_Cnt = BSW_CRYSTAL_COMPENSATION_CNT;

	if(--COC_Cnt == 0)
	{
		COC_Cnt = BSW_CRYSTAL_COMPENSATION_CNT;
		OS_tiSystemTick++;
	}
#endif

	OS_tiSystemTick++;

	for (unsigned char i = 0; i < OS_numTolTaskNum; i++)
	{
		if (OS_tiSystemTick - OS_sch_buf[i].LastTime >= OS_sch_buf[i].CycleTime && OS_sch_buf[i].CycleTime != 0)
		{
			OS_sch_buf[i].Request = 1;
			OS_sch_buf[i].LastTime += OS_sch_buf[i].CycleTime;
		}
	}
}

/*
* Name:BSW_RegTask
* Formal para:fpCycleTime, fpPriority(0~254), fpfun; when cycletime eql 0, this task is a event
* return value:task handler(0~MAX_TASK_NUM-1), 255 means failure
* description:register a task
*/
unsigned char BSW_RegTask(unsigned int fpCycleTime, unsigned char fpPriority, void (*fpfun)(void))
{
	if (OS_numTolTaskNum >= MAX_TASK_NUM || fpfun == NULL || fpPriority == 255)
	{
		return 255;
	}

	OS_sch_buf[OS_numTolTaskNum].CycleTime = fpCycleTime;
	OS_sch_buf[OS_numTolTaskNum].LastTime = OS_tiSystemTick;
	OS_sch_buf[OS_numTolTaskNum].Priority = fpPriority;
	OS_sch_buf[OS_numTolTaskNum].Request = 0;
	OS_sch_buf[OS_numTolTaskNum].fun = fpfun;

	OS_numTolTaskNum++;

	return (OS_numTolTaskNum - 1);
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
* description:start the OS scheduler
*/
void BSW_OsStart(void)
{
	unsigned char LastPriority = 255;
	unsigned char i = 0, k = 255;

#if (BSW_CPU_LOAD_ENABLE == 1)
	/* ---- 负载统计：以启动时刻为统计窗口起点 ---- */
	unsigned int  idleStart = 0;
	unsigned char wasIdle   = 0;

	OS_tiIdleTicks = 0;
	OS_tiResetMark = OS_tiSystemTick;
	idleStart      = OS_tiSystemTick;
	/* -------------------------------------------- */
#endif

	while (1)
	{
		LastPriority = 255;
		k = 255;

		for (i = 0; i < OS_numTolTaskNum; i++)
		{
			if (OS_sch_buf[i].Request == 1 && OS_sch_buf[i].Priority < LastPriority)
			{
				LastPriority = OS_sch_buf[i].Priority;
				k = i;
			}
		}

		if (k != 255)
		{
#if (BSW_CPU_LOAD_ENABLE == 1)
			/* 刚退出空闲：结算上一段空闲时长 */
			if (wasIdle)
			{
				OS_tiIdleTicks += OS_tiSystemTick - idleStart;
				wasIdle = 0;
			}
#endif

			OS_sch_buf[k].Request = 0;
			OS_sch_buf[k].fun();
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
