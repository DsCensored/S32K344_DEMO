/**
 * @file    hse_trng.c
 * @brief   通过 HSE 的 GET_RANDOM_NUM 服务取硬件 TRNG 随机数（供 UDS 0x27 使用）
 *
 * 实现要点（踩过的坑，务必保留）：
 *  1. **服务描述符和输出缓冲区必须放在 HSE 能访问的 SRAM 里**
 *     - HSE 是独立的安全核，读不到 M7 的 DTCM；而本工程的栈在 DTCM
 *       （int_stack_dtcm @0x2001F000），所以绝不能把描述符放在栈上。
 *     - 这里用工程约定的 `.non_cacheable_data` 段（int_sram_no_cacheable），
 *       顺便避开 D-Cache 与 HSE 直接读写内存之间的一致性问题。
 *  2. 仍然做一次 clean/invalidate，防止该段未被 MPU 配成 non-cacheable 时出错。
 *  3. 惰性初始化 + 失败自动重试：HSE 可能比 App 晚准备好；UDS 第一次 0x27
 *     请求时再初始化，避免在启动流程里加重依赖。
 */
#include <string.h>

#include "hse_trng.h"
#include "hse_trng_cfg.h"      /* S32K344.h + MU/RNG 配置 */

#include "hse_interface.h"
#include "hse_host.h"
#include "hse_mu.h"

/*----------------------------------------------------------------------------
 * 与服务端共享的内存：必须放在 HSE 可访问的 SRAM（不要放栈/DTCM）
 *----------------------------------------------------------------------------*/
#if defined(__GNUC__)
#define HSE_TRNG_SHARED_SECTION  __attribute__((section(".non_cacheable_data"), aligned(8)))
#else
#define HSE_TRNG_SHARED_SECTION
#endif

static HSE_TRNG_SHARED_SECTION hseSrvDescriptor_t s_hseDesc;
static HSE_TRNG_SHARED_SECTION uint8_t            s_rngBuf[64U];   /* 够 UDS 用，节省内存 */

/*----------------------------------------------------------------------------
 * 本地状态
 *----------------------------------------------------------------------------*/
static uint8_t  s_initialized = 0U;
static uint8_t  s_channel     = HSE_INVALID_CHANNEL;
static uint8_t  s_lastError    = HSE_TRNG_OK;

/*----------------------------------------------------------------------------
 * 小工具：缓存维护直接用 hse_trng_cfg.h 里的实现（不依赖 CMSIS）
 *----------------------------------------------------------------------------*/
#define HseTrng_CleanDcache(p, l)       HseTrng_CacheClean((p), (l))
#define HseTrng_InvalidateDcache(p, l)  HseTrng_CacheInvalidate((p), (l))

/*----------------------------------------------------------------------------
 * 生命周期
 *----------------------------------------------------------------------------*/
void HseTrng_Reset(void)
{
    s_initialized = 0U;
    s_channel     = HSE_INVALID_CHANNEL;
}

uint8_t HseTrng_IsReady(void)
{
    return s_initialized;
}

uint8_t HseTrng_GetLastError(void)
{
    return s_lastError;
}

uint8_t HseTrng_Init(void)
{
    uint16_t hseStatus;
    uint8_t  channel;

    if (0U != s_initialized)
    {
        return HSE_TRNG_OK;
    }

    /* HSE 是否已经初始化完成（MU0 FSR 的 HSE 状态字） */
    hseStatus = HSE_MU_GetHseStatus(HSE_TRNG_MU_INSTANCE);

    if (0U == (hseStatus & (uint16_t)HSE_STATUS_INIT_OK))
    {
        s_lastError = HSE_TRNG_ERR_NOT_READY;
        return s_lastError;
    }

    /* 随机数服务要求 RNG 已初始化 */
    if (0U == (hseStatus & (uint16_t)HSE_STATUS_RNG_INIT_OK))
    {
        s_lastError = HSE_TRNG_ERR_RNG_NOT_READY;
        return s_lastError;
    }

    /* 申请一个空闲服务通道（通道 0 留给 HSE 管理服务） */
    channel = (uint8_t)HSE_MU_GetFreeChannel(HSE_TRNG_MU_INSTANCE);
    if (HSE_INVALID_CHANNEL == channel)
    {
        s_lastError = HSE_TRNG_ERR_NO_CHANNEL;
        return s_lastError;
    }

    s_channel     = channel;
    s_initialized = 1U;
    s_lastError   = HSE_TRNG_OK;
    return HSE_TRNG_OK;
}

/*----------------------------------------------------------------------------
 * 取随机数
 *----------------------------------------------------------------------------*/
uint8_t HseTrng_GetBytes(uint8_t *pBuf, uint32_t u32Len)
{
    hseSrvResponse_t      hseStatus;
    hseGetRandomNumSrv_t *pReq;
    uint32_t              u32Offset;

    if ((NULL == pBuf) || (0U == u32Len) || (u32Len > sizeof(s_rngBuf)))
    {
        s_lastError = HSE_TRNG_ERR_PARAM;
        return s_lastError;
    }

    if (HSE_TRNG_OK != HseTrng_Init())
    {
        return s_lastError;
    }

    (void)memset(&s_hseDesc, 0, sizeof(s_hseDesc));
    (void)memset(s_rngBuf, 0, u32Len);

    s_hseDesc.srvId = HSE_SRV_ID_GET_RANDOM_NUM;
    pReq = &(s_hseDesc.hseSrv.getRandomNumReq);
    pReq->rngClass        = (hseRngClass_t)HSE_TRNG_RNG_CLASS;
    pReq->randomNumLength = u32Len;
    pReq->pRandomNum      = (HOST_ADDR)s_rngBuf;

    /* 让 HSE 看到最新的描述符与缓冲区内容 */
    HseTrng_CleanDcache(&s_hseDesc, (uint32_t)sizeof(s_hseDesc));
    HseTrng_CleanDcache(s_rngBuf, u32Len);
    HseTrng_Dsb();

    hseStatus = HSE_Send(HSE_TRNG_MU_INSTANCE, s_channel, gSyncTxOption, &s_hseDesc);

    if (HSE_SRV_RSP_OK != hseStatus)
    {
        /* 通道可能被卡住，丢掉它，下次重新申请 */
        s_lastError   = HSE_TRNG_ERR_HSE_STATUS;
        s_initialized = 0U;
        s_channel     = HSE_INVALID_CHANNEL;
        return s_lastError;
    }

    /* HSE 直接写内存，丢弃可能过期的缓存行 */
    HseTrng_InvalidateDcache(s_rngBuf, u32Len);
    HseTrng_Dsb();

    for (u32Offset = 0U; u32Offset < u32Len; u32Offset++)
    {
        pBuf[u32Offset] = s_rngBuf[u32Offset];
    }

    s_lastError = HSE_TRNG_OK;
    return HSE_TRNG_OK;
}

uint32_t HseTrng_GetRandomU32(void)
{
    uint8_t  buf[4] = {0U, 0U, 0U, 0U};
    uint32_t value;

    if (HSE_TRNG_OK != HseTrng_GetBytes(buf, 4U))
    {
        return 0U;      /* 0 = 失败，调用方回退 */
    }

    value = ((uint32_t)buf[0] << 24U) |
            ((uint32_t)buf[1] << 16U) |
            ((uint32_t)buf[2] <<  8U) |
            ((uint32_t)buf[3]);

    /* 0 作为"失败"标志，随机值恰好为 0 时返回 1（不损失熵） */
    return (0U == value) ? 1U : value;
}

uint8_t HseTrng_GetRandomU64(uint64_t *pU64)
{
    uint8_t buf[8] = {0U};

    if (NULL == pU64)
    {
        s_lastError = HSE_TRNG_ERR_PARAM;
        return s_lastError;
    }

    if (HSE_TRNG_OK != HseTrng_GetBytes(buf, 8U))
    {
        return s_lastError;
    }

    *pU64 = ((uint64_t)buf[0] << 56U) | ((uint64_t)buf[1] << 48U) |
            ((uint64_t)buf[2] << 40U) | ((uint64_t)buf[3] << 32U) |
            ((uint64_t)buf[4] << 24U) | ((uint64_t)buf[5] << 16U) |
            ((uint64_t)buf[6] <<  8U) | ((uint64_t)buf[7]);

    return HSE_TRNG_OK;
}

/*----------------------------------------------------------------------------
 * 上电自检（供调试/故障记录用，可用 HSE_TRNG_SELFTEST_ON_BOOT 关掉）
 *----------------------------------------------------------------------------*/
volatile uint32_t g_hseTrngSelfTest[6] = {0xFFFFFFFFUL, 0U, 0xFFFFFFFFUL, 0U, 0U, 0U};

uint8_t HseTrng_SelfTest(void)
{
    uint32_t r1;
    uint32_t r2;
    uint8_t  e1;
    uint8_t  e2;

    HseTrng_Reset();

    r1 = HseTrng_GetRandomU32();
    e1 = HseTrng_GetLastError();

    r2 = HseTrng_GetRandomU32();
    e2 = HseTrng_GetLastError();

    g_hseTrngSelfTest[0] = (uint32_t)e1;
    g_hseTrngSelfTest[1] = r1;
    g_hseTrngSelfTest[2] = (uint32_t)e2;
    g_hseTrngSelfTest[3] = r2;
    g_hseTrngSelfTest[4] = (uint32_t)HSE_MU_GetHseStatus(HSE_TRNG_MU_INSTANCE);
    g_hseTrngSelfTest[5] = 0x5A5A0001UL;

    return (uint8_t)(((0U == e1) && (0U == e2) && (r1 != r2)) ? 0U : 1U);
}