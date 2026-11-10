/**
 * @file    hse_trng.h
 * @brief   HSE 硬件 TRNG 的极简封装，供 UDS 0x27（SecurityAccess）取随机种子
 *
 * 特点：
 *   - 公共头只依赖 <stdint.h>，可以被 AUTOSAR 风格的 UDS 代码直接 include，
 *     不会和 Std_Types / Platform_Types 打架。
 *   - 内部惰性初始化：第一次调用时才检查 HSE 状态、申请 MU 通道，
 *     因此 App 和 Bootloader 都只需要在 0x27 处理函数里调用即可。
 *   - HSE 不可用（未启动 / RNG 未就绪 / 通道忙）时返回非 0，调用方应回退。
 *
 * 用法（UDS 0x27 RequestSeed）：
 *      uint32_t seed = HseTrng_GetRandomU32();
 *      if (0U == seed) { 回退到原来的伪随机方案 }
 */
#ifndef HSE_TRNG_H
#define HSE_TRNG_H

#include <stdint.h>

/** 上电自检开关：1 = BSW_Init() 里跑一次 TRNG 自检（结果放在 g_hseTrngSelfTest[]）。
 *  可在工程宏里覆盖为 0 关掉。放在公共头，保证 BSW 层 #if 能正确判断。 */
#ifndef HSE_TRNG_SELFTEST_ON_BOOT
#define HSE_TRNG_SELFTEST_ON_BOOT   (1U)
#endif

/** 返回码：0 = 成功，非 0 = 失败 */
#define HSE_TRNG_OK                 (0U)
#define HSE_TRNG_ERR_NOT_READY      (1U)   /**< HSE 未启动 / MU 状态异常      */
#define HSE_TRNG_ERR_RNG_NOT_READY  (2U)   /**< HSE 的 RNG 还没初始化好       */
#define HSE_TRNG_ERR_NO_CHANNEL     (3U)   /**< 没有空闲 MU 通道              */
#define HSE_TRNG_ERR_PARAM          (4U)   /**< 参数非法（空指针/长度超限）   */
#define HSE_TRNG_ERR_HSE_STATUS     (5U)   /**< HSE 服务返回非 OK             */

/**
 * @brief  初始化（惰性）：检查 HSE 是否就绪并申请一个 MU 通道。
 * @return 0 = 就绪；非 0 = 失败原因（见上面的返回码）
 */
uint8_t HseTrng_Init(void);

/**
 * @brief  从 HSE TRNG 取任意长度随机数（1..512 字节）
 * @param  pBuf   目的缓冲区
 * @param  u32Len 字节数
 * @return 0 = 成功
 */
uint8_t HseTrng_GetBytes(uint8_t *pBuf, uint32_t u32Len);

/**
 * @brief  取一个 32 位随机数（给 UDS 0x27 用）
 * @return 随机值；失败时返回 0（绝不会把 0 当有效随机值返回）
 */
uint32_t HseTrng_GetRandomU32(void);

/**
 * @brief  取一个 64 位随机数（若以后 UDS 种子要 8 字节）
 * @param  pU64 输出
 * @return 0 = 成功
 */
uint8_t HseTrng_GetRandomU64(uint64_t *pU64);

/**
 * @brief  当前是否已经初始化成功（不发请求）
 */
uint8_t HseTrng_IsReady(void);

/**
 * @brief  复位封装层状态（例如 HSE 重新初始化后调用，下次访问会重新申请通道）
 */
void HseTrng_Reset(void);

/**
 * @brief  最近一次失败的原因（0 = 无错误），便于调试/故障记录
 */
uint8_t HseTrng_GetLastError(void);

/** 上电自检结果（调试器可直接读，也可用于故障记录）：
 *    [0] 第 1 次调用返回码      [1] 第 1 个随机数
 *    [2] 第 2 次调用返回码      [3] 第 2 个随机数
 *    [4] 自检时的 HSE 状态字    [5] 0x5A5A0001（标记自检已执行） */
extern volatile uint32_t g_hseTrngSelfTest[6];

/**
 * @brief  HSE TRNG 自检：连续取两个随机数，判断是否都成功且互不相同
 * @return 0 = 正常；非 0 = 异常（细节见 g_hseTrngSelfTest）
 */
uint8_t HseTrng_SelfTest(void);

#endif /* HSE_TRNG_H */
