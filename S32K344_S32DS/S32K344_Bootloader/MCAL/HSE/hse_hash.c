/**
 * @file    hse_hash.c
 * @brief   HSE SHA-256 for the bootloader programming services.
 *
 * The HSE HASH service is used in streaming mode (SUF):
 *
 *      HSE_ACCESS_MODE_START   : reset the hash context of the stream
 *      HSE_ACCESS_MODE_UPDATE  : feed whole 64-byte blocks (any count)
 *      HSE_ACCESS_MODE_FINISH  : feed the remaining bytes, return the digest
 *
 * This is the same sequence as the reference demo's
 * HashDataStartStreamSrv() / HashDataUpdateStreamSrv() /
 * HashDataFinishStreamSrv() (framework/host_crypto_helper/hse_host_hash.c).
 *
 * Restrictions taken from hse_srv_hash.h:
 *   - START   : inputLength must be a multiple of the block length, or zero,
 *   - UPDATE  : inputLength must be a multiple of 64 and must not be zero,
 *   - FINISH  : inputLength can have any value.
 *
 * HSE cannot read the M7 DTCM/ITCM (the stack lives there and the RAM flash
 * driver is downloaded to ITCM address 0), therefore data that lives in the
 * core TCM is copied into a staging buffer placed in .non_cacheable_data.
 *
 * Data that lives in PFlash is handed to the HSE directly (it reads the flash
 * itself): no copy, and one MU round trip per HSE_HASH_DIRECT_CHUNK instead of
 * per HSE_HASH_STAGE_SIZE bytes.  This is the path used for the APP image at
 * start-up, where every millisecond counts.
 */
#include <string.h>

#include "hse_hash.h"
#include "hse_trng_cfg.h"      /* shared section + D-cache helpers */

#include "hse_interface.h"
#include "hse_host.h"
#include "hse_mu.h"

#if defined(__GNUC__)
#define HSE_HASH_SHARED  __attribute__((section(".non_cacheable_data"), aligned(16)))
#else
#define HSE_HASH_SHARED
#endif

/* SHA-256 uses one stream; a second one is available (HSE_STREAM_COUNT = 2)
 * but not needed here. */
#define HSE_HASH_STREAM_ID      (0U)

/* Staging buffer size: multiple of the SHA-256 block size, so an UPDATE can
 * always be split into whole blocks.  8 KB keeps the number of MU round trips
 * low for the images that must go through SRAM (ITCM flash driver, RAM
 * buffers) without using much of the 64 KB non-cacheable SRAM region. */
#define HSE_HASH_STAGE_SIZE     (8192U)

/* Direct PFlash requests: one MU round trip for this many bytes.  Multiple of
 * the SHA-256 block size (and of the staging buffer size). */
#define HSE_HASH_DIRECT_CHUNK   (65536U)

/* S32K344 code flash window (PFlash 0x00400000..0x007FFFFF).  The HSE can read
 * it, the M7 TCM (ITCM 0x00000000 / DTCM 0x2000xxxx) it cannot. */
#define HSE_HASH_PFLASH_BASE    (0x00400000UL)
#define HSE_HASH_PFLASH_END     (0x00800000UL)

/* Buffers shared with the HSE: system SRAM, never the DTCM/stack. */
static HSE_HASH_SHARED hseSrvDescriptor_t s_desc;
static HSE_HASH_SHARED uint8_t            s_stage[HSE_HASH_STAGE_SIZE];
static HSE_HASH_SHARED uint8_t            s_digest[HSE_HASH_SHA256_SIZE];
static HSE_HASH_SHARED uint32_t           s_digestLen;

/* Debug view for the J-Link:
 *   [0] HSE status at init        [1] last raw HSE response
 *   [2] last error code           [3] digest length returned by the HSE
 *   [4] number of UPDATE requests [5] 0x5A5A4841 "HASH" marker
 */
volatile uint32_t g_hseHashDiag[6] = {
    0xFFFFFFFFUL, 0U, 0U, 0U, 0U, 0x5A5A4841UL
};

static uint8_t  s_channel   = HSE_INVALID_CHANNEL;
static uint8_t  s_lastError = HSE_HASH_OK;
static uint32_t s_lastRsp   = 0U;

/*---------------------------------------------------------------------------
 * Low level helpers
 *-------------------------------------------------------------------------*/
static uint8_t HseHash_Fail(uint8_t err)
{
    s_lastError      = err;
    g_hseHashDiag[2] = (uint32_t)err;
    return err;
}

static uint8_t HseHash_Send(void)
{
    hseSrvResponse_t rsp;

    if (HSE_INVALID_CHANNEL == s_channel)
    {
        s_channel = (uint8_t)HSE_MU_GetFreeChannel(HSE_TRNG_MU_INSTANCE);
        if (HSE_INVALID_CHANNEL == s_channel)
        {
            s_lastRsp        = 0xFFFFFFFEUL;
            g_hseHashDiag[1] = s_lastRsp;
            return HseHash_Fail(HSE_HASH_ERR_NO_CHANNEL);
        }
    }

    rsp = HSE_Send(HSE_TRNG_MU_INSTANCE, s_channel, gSyncTxOption, &s_desc);
    s_lastRsp        = (uint32_t)rsp;
    g_hseHashDiag[1] = s_lastRsp;

    if (HSE_SRV_RSP_OK != rsp)
    {
        /* Drop the channel so a stuck response cannot poison the next
         * request; the next call obtains a free channel again. */
        s_channel = HSE_INVALID_CHANNEL;
        return HseHash_Fail(HSE_HASH_ERR_HSE_STATUS);
    }

    s_lastError = HSE_HASH_OK;
    return HSE_HASH_OK;
}

/* Fill the service descriptor and make it visible to the HSE. */
static void HseHash_BuildRequest(hseAccessMode_t accessMode,
                                 const uint8_t *pInput, uint32_t u32Len)
{
    hseHashSrv_t *p;

    (void)memset(&s_desc, 0, sizeof(s_desc));
    s_desc.srvId = HSE_SRV_ID_HASH;

    p = &(s_desc.hseSrv.hashReq);
    p->accessMode  = accessMode;
    p->streamId    = (hseStreamId_t)HSE_HASH_STREAM_ID;
    p->hashAlgo    = HSE_HASH_ALGO_SHA2_256;
    p->sgtOption   = HSE_SGT_OPTION_NONE;
    p->inputLength = u32Len;
    p->pInput      = (HOST_ADDR)pInput;

    if (HSE_ACCESS_MODE_FINISH == accessMode)
    {
        s_digestLen = HSE_HASH_SHA256_SIZE;

        p->pHashLength = (HOST_ADDR)&s_digestLen;
        p->pHash       = (HOST_ADDR)s_digest;

        /* pHashLength is an in/out parameter (buffer size in, digest length
         * out) and pHash is written by the HSE: push the size out and drop
         * stale cache lines of the output buffer. */
        HseTrng_CacheClean(&s_digestLen, (uint32_t)sizeof(s_digestLen));
        HseTrng_CacheInvalidate(s_digest, (uint32_t)sizeof(s_digest));
    }

    HseTrng_CacheClean(&s_desc, (uint32_t)sizeof(s_desc));
}

/*---------------------------------------------------------------------------
 * Local helpers: direct PFlash transfer
 *-------------------------------------------------------------------------*/
/* Whole PFlash range?  Only there the HSE may read the caller data directly. */
static uint8_t HseHash_IsPflash(const uint8_t *pData, uint32_t u32Len)
{
    uint32_t base = (uint32_t)pData;
    uint32_t end  = base + u32Len;

    if ((0U == u32Len) || (end < base))
    {
        return 0U;
    }

    return ((base >= (uint32_t)HSE_HASH_PFLASH_BASE) &&
            (end  <= (uint32_t)HSE_HASH_PFLASH_END)) ? 1U : 0U;
}

/* Feed PFlash data without the staging buffer: the HSE reads the flash itself
 * and only the service descriptor must be visible to it.  The source is
 * read-only, so no D-Cache maintenance is needed for it. */
static uint8_t HseHash_UpdateDirect(const uint8_t *pData, uint32_t u32Len)
{
    uint8_t  err;
    uint32_t offset = 0U;

    if (0U == u32Len)
    {
        return HSE_HASH_OK;
    }

    err = HseHash_Init();
    if (HSE_HASH_OK != err)
    {
        return err;
    }

    while (offset < u32Len)
    {
        uint32_t chunk = u32Len - offset;

        if (chunk > (uint32_t)HSE_HASH_DIRECT_CHUNK)
        {
            chunk = (uint32_t)HSE_HASH_DIRECT_CHUNK;
        }

        HseHash_BuildRequest(HSE_ACCESS_MODE_UPDATE, &pData[offset], chunk);
        HseTrng_Dsb();

        err = HseHash_Send();
        if (HSE_HASH_OK != err)
        {
            return err;
        }

        g_hseHashDiag[4]++;
        offset += chunk;
    }

    return HSE_HASH_OK;
}

/*---------------------------------------------------------------------------
 * Public API
 *-------------------------------------------------------------------------*/
uint8_t HseHash_Init(void)
{
    uint16_t status = HSE_MU_GetHseStatus(HSE_TRNG_MU_INSTANCE);

    g_hseHashDiag[0] = (uint32_t)status;

    if (0U == (status & (uint16_t)HSE_STATUS_INIT_OK))
    {
        s_lastRsp = 0xFFFFFFE1UL;
        g_hseHashDiag[1] = s_lastRsp;
        return HseHash_Fail(HSE_HASH_ERR_NOT_READY);
    }

    return HSE_HASH_OK;
}

uint8_t HseHash_WaitReady(uint32_t u32Retries, uint32_t u32Delay)
{
    uint32_t retry;
    volatile uint32_t delay;

    for (retry = 0U; retry < u32Retries; retry++)
    {
        if (HSE_HASH_OK == HseHash_Init())
        {
            return HSE_HASH_OK;
        }

        for (delay = 0U; delay < u32Delay; delay++)
        {
            /* busy wait */
        }
    }

    return HSE_HASH_ERR_NOT_READY;
}

uint8_t HseHash_Start(void)
{
    uint8_t err = HseHash_Init();

    if (HSE_HASH_OK != err)
    {
        return err;
    }

    /* START is also the recovery point of an aborted stream. */
    HseHash_BuildRequest(HSE_ACCESS_MODE_START, 0, 0U);
    HseTrng_Dsb();

    return HseHash_Send();
}

uint8_t HseHash_Update(const uint8_t *pData, uint32_t u32Len)
{
    uint8_t  err;
    uint32_t offset = 0U;

    if (0U == u32Len)
    {
        return HSE_HASH_OK;             /* nothing to feed, HSE forbids it */
    }

    /* No null-pointer test on pData: on S32K3 address 0x00000000 is the ITCM
     * and the flash driver image is downloaded there (Boot_Flash.h). */
    if (0U != (u32Len % HSE_HASH_SHA256_BLOCK))
    {
        return HseHash_Fail(HSE_HASH_ERR_PARAM);
    }

    err = HseHash_Init();
    if (HSE_HASH_OK != err)
    {
        return err;
    }

    while (offset < u32Len)
    {
        uint32_t chunk = u32Len - offset;

        if (chunk > HSE_HASH_STAGE_SIZE)
        {
            chunk = HSE_HASH_STAGE_SIZE;
        }

        (void)memcpy(s_stage, &pData[offset], chunk);
        HseHash_BuildRequest(HSE_ACCESS_MODE_UPDATE, s_stage, chunk);
        HseTrng_CacheClean(s_stage, chunk);
        HseTrng_Dsb();

        err = HseHash_Send();
        if (HSE_HASH_OK != err)
        {
            return err;
        }

        g_hseHashDiag[4]++;
        offset += chunk;
    }

    return HSE_HASH_OK;
}

uint8_t HseHash_Finish(const uint8_t *pData, uint32_t u32Len,
                       uint8_t *pDigest)
{
    uint8_t err;

    if ((0 == pDigest) || (u32Len > HSE_HASH_STAGE_SIZE))
    {
        return HseHash_Fail(HSE_HASH_ERR_PARAM);
    }

    err = HseHash_Init();
    if (HSE_HASH_OK != err)
    {
        return err;
    }

    if (0U != u32Len)
    {
        (void)memcpy(s_stage, pData, u32Len);
    }

    HseHash_BuildRequest(HSE_ACCESS_MODE_FINISH,
                         (0U != u32Len) ? s_stage : 0, u32Len);

    if (0U != u32Len)
    {
        HseTrng_CacheClean(s_stage, u32Len);
    }
    HseTrng_Dsb();

    err = HseHash_Send();
    if (HSE_HASH_OK != err)
    {
        return err;
    }

    /* The HSE wrote the digest and the digest length directly to SRAM. */
    HseTrng_CacheInvalidate(s_digest, (uint32_t)sizeof(s_digest));
    HseTrng_CacheInvalidate(&s_digestLen, (uint32_t)sizeof(s_digestLen));
    HseTrng_Dsb();

    g_hseHashDiag[3] = s_digestLen;

    if (HSE_HASH_SHA256_SIZE != s_digestLen)
    {
        return HseHash_Fail(HSE_HASH_ERR_DIGEST_LEN);
    }

    (void)memcpy(pDigest, s_digest, HSE_HASH_SHA256_SIZE);

    return HSE_HASH_OK;
}

uint8_t HseHash_Sha256Region(const uint8_t *pData, uint32_t u32Len,
                             uint8_t *pDigest)
{
    uint32_t full;
    uint8_t  err;

    if (0 == pDigest)
    {
        return HseHash_Fail(HSE_HASH_ERR_PARAM);
    }

    /* Whole blocks can be fed with UPDATE; the tail (< 64 bytes) must go
     * through FINISH. */
    full = u32Len & ~(uint32_t)(HSE_HASH_SHA256_BLOCK - 1U);

    /* Fast path for a PFlash image (the APP run/backup area): the HSE reads
     * the flash itself, so the bytes are not copied through SRAM and one MU
     * round trip covers HSE_HASH_DIRECT_CHUNK instead of HSE_HASH_STAGE_SIZE
     * bytes.  pData is never tested against zero: address 0 is the ITCM. */
    if (0U != HseHash_IsPflash(pData, u32Len))
    {
        err = HseHash_Start();
        if (HSE_HASH_OK == err)
        {
            err = HseHash_UpdateDirect(pData, full);
        }
        if (HSE_HASH_OK == err)
        {
            return HseHash_Finish(&pData[full], u32Len - full, pDigest);
        }

        /* The direct request was refused: START resets the hash context, so
         * the generic staged path below starts from a clean stream. */
    }

    err = HseHash_Start();
    if (HSE_HASH_OK != err)
    {
        return err;
    }

    err = HseHash_Update(pData, full);
    if (HSE_HASH_OK != err)
    {
        return err;
    }

    return HseHash_Finish(&pData[full], u32Len - full, pDigest);
}

uint8_t HseHash_GetLastError(void)
{
    return s_lastError;
}

uint32_t HseHash_GetLastResponse(void)
{
    return s_lastRsp;
}
