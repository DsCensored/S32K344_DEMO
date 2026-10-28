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

void BSW_IsrPit0Ch0(void);
unsigned int BSW_GetSystemTick(void);
unsigned int BSW_GetElapsedTime(unsigned int LastTime);
unsigned char BSW_RegTask(unsigned int fpCycleTime, unsigned char fpPriority, void (*fpfun)(void));
void BSW_OsStart(void);

unsigned char BSW_GetCpuLoad(void);
void BSW_ResetCpuLoadStat(void);

#endif
