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
 * HSE cannot read the M7 DTCM (the stack lives there), therefore the caller
 * data is copied into a staging buffer placed in .non_cacheable_data and the
 * D-Cache is cleaned before every request.
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
 * always be split into whole blocks.  2 KB keeps the number of MU round
 * trips low while hashing a 1.5 MB application image (~800 requests). */
#define HSE_HASH_STAGE_SIZE     (2048U)

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

    /* pData is not checked against zero: the ITCM lives at address 0. */
    err = HseHash_Start();
    if (HSE_HASH_OK != err)
    {
        return err;
    }

    /* Whole blocks can be fed with UPDATE; the tail (< 64 bytes) must go
     * through FINISH. */
    full = u32Len & ~(uint32_t)(HSE_HASH_SHA256_BLOCK - 1U);

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
