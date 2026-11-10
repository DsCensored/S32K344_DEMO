/**
 * @file    hse_trng_cfg.h
 * @brief   HSE TRNG 封装的适配层（唯一需要按工程/芯片改动的地方）
 *
 * 为什么不用 CMSIS：
 *   本工程（S32K344 RTD）里没有 core_cm7.h / __disable_irq，AUTOSAR 那边用的是
 *   OsIf_SuspendAllInterrupts；而 HSE 例程的 hse_host.c 依赖 __disable_irq。
 *   为了不给工程引入 CMSIS 依赖（也避免和 Std_Types 的 TRUE/FALSE 冲突），
 *   这里直接用两条 Cortex-M 内联汇编提供"关/开全局中断"，并自己实现 DSB 与
 *   D-Cache 维护。
 */
#ifndef HSE_TRNG_CFG_H
#define HSE_TRNG_CFG_H

#include <stdint.h>

/*----------------------------------------------------------------------------
 * 1. 平台选择
 *    HSE 接口里 HSE_PLATFORM / HSE_FWTYPE 已在 interface/config/hse_target.h
 *    固定为 S32K3X4 + STANDARD，无需额外 -D。
 *----------------------------------------------------------------------------*/

/** 使用的 MU 实例：0 = MU0（HSE 与 M7 之间的 MU） */
#define HSE_TRNG_MU_INSTANCE    (0U)

/** UDS 27 种子用的 TRNG 类别：
 *  HSE_RNG_CLASS_DRG3 (0) 最快，无预测抵抗
 *  HSE_RNG_CLASS_DRG4 (1) 每次调用重新播种（预测抵抗）——安全相关推荐
 *  HSE_RNG_CLASS_PTG3 (2) 最慢，每 16 字节重新播种
 *  这里用数值而不是宏，避免公共头依赖 HSE 的接口头。 */
#define HSE_TRNG_RNG_CLASS      (1U)

/** 单次最大请求字节数（HSE 限制 512） */
#define HSE_TRNG_MAX_LEN        (512U)

/*----------------------------------------------------------------------------
 * 2. 中断/屏障原语（替代 CMSIS 的 __disable_irq / __enable_irq / __DSB）
 *----------------------------------------------------------------------------*/
static inline void HseTrng_DisableIrq(void)
{
    __asm volatile ("cpsid i" ::: "memory");
}

static inline void HseTrng_EnableIrq(void)
{
    __asm volatile ("cpsie i" ::: "memory");
}

static inline void HseTrng_Dsb(void)
{
    __asm volatile ("dsb sy" ::: "memory");
}

/* hse_host.c 里的两个宏名沿用 NXP 例程 */
#define sys_disableAllInterrupts    HseTrng_DisableIrq
#define sys_enableAllInterrupts     HseTrng_EnableIrq

/*----------------------------------------------------------------------------
 * 3. D-Cache 维护（HSE 直接读写我们给它的内存，需要保证一致性）
 *    工程里 D-Cache 由 Mcl/Cache_Ip 打开，这里用 SCS 寄存器自行维护。
 *----------------------------------------------------------------------------*/
#define HSE_TRNG_SCS_CCR        (*(volatile uint32_t *)0xE000ED14UL)  /* SCB->CCR */
#define HSE_TRNG_SCS_DCCMVAC    (*(volatile uint32_t *)0xE000EF68UL)  /* clean by MVA  */
#define HSE_TRNG_SCS_DCIMVAC    (*(volatile uint32_t *)0xE000EF5CUL)  /* invalidate by MVA */

#define HSE_TRNG_CCR_DC_MASK    (1UL << 16U)   /* CCR.DC: D-Cache enable */

static inline void HseTrng_CacheClean(void *pAddr, uint32_t u32Len)
{
    uint32_t addr = ((uint32_t)pAddr) & ~0x1FUL;
    uint32_t end  = ((uint32_t)pAddr) + u32Len;

    if (0U == (HSE_TRNG_SCS_CCR & HSE_TRNG_CCR_DC_MASK))
    {
        return;                             /* D-Cache 未使能，无需维护 */
    }
    __asm volatile ("dsb sy" ::: "memory");
    while (addr < end)
    {
        HSE_TRNG_SCS_DCCMVAC = addr;
        addr += 32U;
    }
    __asm volatile ("dsb sy" ::: "memory");
    __asm volatile ("dmb sy" ::: "memory");
}

static inline void HseTrng_CacheInvalidate(void *pAddr, uint32_t u32Len)
{
    uint32_t addr = ((uint32_t)pAddr) & ~0x1FUL;
    uint32_t end  = ((uint32_t)pAddr) + u32Len;

    if (0U == (HSE_TRNG_SCS_CCR & HSE_TRNG_CCR_DC_MASK))
    {
        return;
    }
    __asm volatile ("dsb sy" ::: "memory");
    while (addr < end)
    {
        HSE_TRNG_SCS_DCIMVAC = addr;
        addr += 32U;
    }
    __asm volatile ("dsb sy" ::: "memory");
    __asm volatile ("dmb sy" ::: "memory");
}

#endif /* HSE_TRNG_CFG_H */
