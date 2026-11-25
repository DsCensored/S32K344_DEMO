/**
 * @file    hse_ecc.c
 * @brief   HSE ECDSA P-521 (SECP521R1) signature verification for UDS 0x27.
 *
 * The ECU generates the seed R with the HSE TRNG and sends it in plaintext.
 * The tester signs SHA-256(R) with its private key; the ECU verifies that
 * signature with the public key stored in the HSE.  Only the public key is
 * present in the ECU.
 *
 * Key catalog layout (shared with hse_aes.c / hse_rsa.c):
 *   NVM group 0 : AES-128, slots 0/1   (legacy 0x27, disabled)
 *   NVM group 1 : RSA-2048 public key  (bootloader image signature)
 *   NVM group 2 : ECC P-521 public key (0x27, imported by AA01)
 *
 * All HSE shared buffers live in .non_cacheable_data because the HSE is a
 * separate core and cannot access the M7 DTCM (where the stack lives).
 */
#include <string.h>

#include "hse_ecc.h"
#include "hse_ecc_key.h"
#include "hse_crypto_cfg.h"
#include "hse_trng_cfg.h"      /* shared section + D-cache helpers */

#if UDS27_ALGO_ECC521

#include "hse_interface.h"
#include "hse_host.h"
#include "hse_mu.h"

#if defined(__GNUC__)
#define HSE_ECC_SHARED  __attribute__((section(".non_cacheable_data"), aligned(16)))
#else
#define HSE_ECC_SHARED
#endif

/* NVM catalog layout used by both the bootloader and the application. */
#define HSE_ECC_KEY_GROUP       (2U)
#define HSE_ECC_KEY_SLOT        (0U)
#define HSE_ECC_KEY_HANDLE \
    ((hseKeyHandle_t)GET_KEY_HANDLE(HSE_KEY_CATALOG_ID_NVM, HSE_ECC_KEY_GROUP, HSE_ECC_KEY_SLOT))

/* Digest used before the ECDSA operation.  SHA-256 matches the frame format
 * documented in UDS27_ECC521.md; switch to HSE_HASH_ALGO_SHA2_512 if the
 * tester signs with SHA-512. */
#define HSE_ECC_HASH_ALGO       HSE_HASH_ALGO_SHA2_256

static HSE_ECC_SHARED hseSrvDescriptor_t s_desc;
static HSE_ECC_SHARED hseKeyInfo_t       s_keyInfo;
static HSE_ECC_SHARED hseKeyInfo_t       s_infoOut;
static HSE_ECC_SHARED uint8_t            s_pubBuf[HSE_ECC_P521_PUB_LEN];
static HSE_ECC_SHARED uint8_t            s_inBuf[HSE_ECC_RANDOM_LEN];
static HSE_ECC_SHARED uint8_t            s_rBuf[HSE_ECC_SIG_COMP_LEN];
static HSE_ECC_SHARED uint8_t            s_sBuf[HSE_ECC_SIG_COMP_LEN];
static HSE_ECC_SHARED uint32_t           s_rLen;
static HSE_ECC_SHARED uint32_t           s_sLen;

/* Debug view for the J-Link:
 *   [0] HSE status at init  [1] key-exists result  [2] reserved
 *   [3] import response     [4] verify response    [5] reserved
 *   [6] last error code     [7] last raw HSE response
 */
volatile uint32_t g_hseEccDiag[8];

static uint8_t  s_channel  = 0xFFU;
static uint8_t  s_keyReady = 0U;
static uint32_t s_lastRsp  = 0U;

/*---------------------------------------------------------------------------
 * Low level helpers
 *-------------------------------------------------------------------------*/
static uint8_t HseEcc_Send(void)
{
    hseSrvResponse_t rsp;

    if (0xFFU == s_channel)
    {
        s_channel = (uint8_t)HSE_MU_GetFreeChannel(HSE_TRNG_MU_INSTANCE);
        if (HSE_INVALID_CHANNEL == s_channel)
        {
            s_channel = 0xFFU;
            s_lastRsp = 0xFFFFFFFEUL;
            g_hseEccDiag[7] = s_lastRsp;
            return 0xFEU;
        }
    }

    rsp = HSE_Send(HSE_TRNG_MU_INSTANCE, s_channel, gSyncTxOption, &s_desc);
    s_lastRsp = (uint32_t)rsp;
    g_hseEccDiag[7] = s_lastRsp;

    if (HSE_SRV_RSP_OK != rsp)
    {
        /* Drop the channel so a stuck response cannot poison the next call. */
        s_channel = 0xFFU;
        return (uint8_t)((uint32_t)rsp & 0xFFU);
    }

    return 0U;
}

uint8_t HseEcc_Init(void)
{
    uint16_t status = HSE_MU_GetHseStatus(HSE_TRNG_MU_INSTANCE);

    g_hseEccDiag[0] = (uint32_t)status;
    if (0U == (status & (uint16_t)HSE_STATUS_INIT_OK))
    {
        s_lastRsp = 0xFFFFFFE1UL;
        g_hseEccDiag[7] = s_lastRsp;
        return 0xE1U;                 /* HSE not ready yet */
    }

    return 0U;
}

static uint8_t HseEcc_KeyExists(void)
{
    (void)memset(&s_infoOut, 0, sizeof(s_infoOut));
    (void)memset(&s_desc, 0, sizeof(s_desc));
    s_desc.srvId = HSE_SRV_ID_GET_KEY_INFO;
    s_desc.hseSrv.getKeyInfoReq.keyHandle = HSE_ECC_KEY_HANDLE;
    s_desc.hseSrv.getKeyInfoReq.pKeyInfo  = (HOST_ADDR)&s_infoOut;

    HseTrng_CacheClean(&s_desc, (uint32_t)sizeof(s_desc));
    HseTrng_CacheClean(&s_infoOut, (uint32_t)sizeof(s_infoOut));
    HseTrng_Dsb();

    (void)HseEcc_Send();
    g_hseEccDiag[1] = s_lastRsp;
    return (HSE_SRV_RSP_OK == (hseSrvResponse_t)s_lastRsp) ? 1U : 0U;
}

/*---------------------------------------------------------------------------
 * Public key installation (0x31 01 AA01)
 *-------------------------------------------------------------------------*/
uint8_t HseEcc_ProgramPublicKey(void)
{
    uint8_t err;

    if (0U != HseEcc_Init())
    {
        return 0xE1U;
    }

    /* HSE reads the key material from host memory: use the non-cacheable
     * staging buffer instead of the flash-resident const array. */
    (void)memcpy(s_pubBuf, g_hseEccP521PublicKey, HSE_ECC_P521_PUB_LEN);

    (void)memset(&s_keyInfo, 0, sizeof(s_keyInfo));
    s_keyInfo.keyFlags   = (hseKeyFlags_t)HSE_KF_USAGE_VERIFY;
    s_keyInfo.keyBitLen  = (uint16_t)HSE_KEY521_BITS;
    s_keyInfo.keyCounter = 1UL;      /* NVM anti-replay counter, catalog is fresh */
    s_keyInfo.keyType    = HSE_KEY_TYPE_ECC_PUB;
    s_keyInfo.specific.eccCurveId = HSE_EC_SEC_SECP521R1;

    (void)memset(&s_desc, 0, sizeof(s_desc));
    s_desc.srvId = HSE_SRV_ID_IMPORT_KEY;
    s_desc.hseSrv.importKeyReq.targetKeyHandle = HSE_ECC_KEY_HANDLE;
    s_desc.hseSrv.importKeyReq.pKeyInfo        = (HOST_ADDR)&s_keyInfo;
    /* ECC public key = X || Y (no 0x04 prefix). */
    s_desc.hseSrv.importKeyReq.pKey[0]         = (HOST_ADDR)s_pubBuf;
    s_desc.hseSrv.importKeyReq.keyLen[0]       = (uint16_t)HSE_ECC_P521_PUB_LEN;
    s_desc.hseSrv.importKeyReq.pKey[1]         = (HOST_ADDR)0;
    s_desc.hseSrv.importKeyReq.keyLen[1]       = 0U;
    s_desc.hseSrv.importKeyReq.pKey[2]         = (HOST_ADDR)0;
    s_desc.hseSrv.importKeyReq.keyLen[2]       = 0U;
    /* Both optional key handles must be explicitly invalid; a zeroed value
     * would select NVM group 0 slot 0 as provision key. */
    s_desc.hseSrv.importKeyReq.cipher.cipherKeyHandle     = HSE_INVALID_KEY_HANDLE;
    s_desc.hseSrv.importKeyReq.keyContainer.authKeyHandle = HSE_INVALID_KEY_HANDLE;

    HseTrng_CacheClean(&s_keyInfo, (uint32_t)sizeof(s_keyInfo));
    HseTrng_CacheClean(s_pubBuf, HSE_ECC_P521_PUB_LEN);
    HseTrng_CacheClean(&s_desc, (uint32_t)sizeof(s_desc));
    HseTrng_Dsb();

    err = HseEcc_Send();
    g_hseEccDiag[3] = s_lastRsp;

    if (0U != err)
    {
        return err;
    }

    s_keyReady = 1U;
    return 0U;
}

uint8_t HseEcc_EnsureKey(void)
{
    if (0U != s_keyReady)
    {
        return 0U;
    }

    if (0U != HseEcc_Init())
    {
        return 0xE1U;
    }

    /* Normal case after AA01 / after a power cycle: the NVM key is already
     * there.  The application never imports the key itself. */
    if (0U != HseEcc_KeyExists())
    {
        s_keyReady = 1U;
        return 0U;
    }

    s_lastRsp = 0xFFFFFFE2UL;
    g_hseEccDiag[6] = 0xE2U;
    return 0xE2U;
}

/*---------------------------------------------------------------------------
 * ECDSA verify
 *-------------------------------------------------------------------------*/
static void HseEcc_PadCopy(uint8_t *pDst, const uint8_t *pSrc, uint32_t u32Len)
{
    (void)memset(pDst, 0, HSE_ECC_SIG_COMP_LEN);
    (void)memcpy(&pDst[HSE_ECC_SIG_COMP_LEN - u32Len], pSrc, u32Len);
}

uint8_t HseEcc_Verify(const uint8_t *pMsg, uint32_t u32Len,
                      const uint8_t *pR, uint16_t u16RLen,
                      const uint8_t *pS, uint16_t u16SLen)
{
    hseSignSrv_t *p;
    uint8_t       err;

    g_hseEccDiag[6] = 0xFFFFFFFFUL;

    if ((0 == pMsg) || (0 == pR) || (0 == pS) ||
        (0U == u32Len) || (u32Len > HSE_ECC_RANDOM_LEN) ||
        (0U == u16RLen) || (u16RLen > HSE_ECC_SIG_COMP_LEN) ||
        (0U == u16SLen) || (u16SLen > HSE_ECC_SIG_COMP_LEN))
    {
        g_hseEccDiag[6] = 0xF1U;
        return 0xF1U;
    }

    err = HseEcc_EnsureKey();
    if (0U != err)
    {
        g_hseEccDiag[6] = err;
        return err;
    }

    (void)memcpy(s_inBuf, pMsg, u32Len);
    /* HSE works with fixed-size (r,s) buffers; accept a shorter component
     * by right-aligning it into the 66-byte staging buffer. */
    HseEcc_PadCopy(s_rBuf, pR, (uint32_t)u16RLen);
    HseEcc_PadCopy(s_sBuf, pS, (uint32_t)u16SLen);
    s_rLen = (uint32_t)HSE_ECC_SIG_COMP_LEN;
    s_sLen = (uint32_t)HSE_ECC_SIG_COMP_LEN;

    (void)memset(&s_desc, 0, sizeof(s_desc));
    s_desc.srvId = HSE_SRV_ID_SIGN;
    p = &s_desc.hseSrv.signReq;
    p->accessMode      = HSE_ACCESS_MODE_ONE_PASS;
    p->authDir         = HSE_AUTH_DIR_VERIFY;
    p->bInputIsHashed  = (bool_t)0U;
    p->signScheme.signSch = HSE_SIGN_ECDSA;
    p->signScheme.sch.ecdsa.hashAlgo = HSE_ECC_HASH_ALGO;
    p->keyHandle       = HSE_ECC_KEY_HANDLE;
    p->sgtOption       = HSE_SGT_OPTION_NONE;
    p->inputLength     = u32Len;
    p->pInput          = (HOST_ADDR)s_inBuf;
    p->pSignatureLength[0] = (HOST_ADDR)&s_rLen;
    p->pSignatureLength[1] = (HOST_ADDR)&s_sLen;
    p->pSignature[0]       = (HOST_ADDR)s_rBuf;
    p->pSignature[1]       = (HOST_ADDR)s_sBuf;

    HseTrng_CacheClean(s_inBuf, u32Len);
    HseTrng_CacheClean(s_rBuf, (uint32_t)sizeof(s_rBuf));
    HseTrng_CacheClean(s_sBuf, (uint32_t)sizeof(s_sBuf));
    HseTrng_CacheClean(&s_desc, (uint32_t)sizeof(s_desc));
    HseTrng_CacheClean(&s_rLen, (uint32_t)sizeof(s_rLen));
    HseTrng_CacheClean(&s_sLen, (uint32_t)sizeof(s_sLen));
    HseTrng_Dsb();

    err = HseEcc_Send();
    g_hseEccDiag[4] = s_lastRsp;

    if (0U != err)
    {
        if (HSE_SRV_RSP_VERIFY_FAILED == (hseSrvResponse_t)s_lastRsp)
        {
            g_hseEccDiag[6] = HSE_ECC_ERR_VERIFY;
            return HSE_ECC_ERR_VERIFY;
        }

        g_hseEccDiag[6] = err;
        return err;
    }

    g_hseEccDiag[6] = 0U;
    return 0U;
}

uint8_t HseEcc_GetLastError(void)
{
    return (uint8_t)(s_lastRsp & 0xFFU);
}

uint32_t HseEcc_GetLastResponse(void)
{
    return s_lastRsp;
}

#endif /* UDS27_ALGO_ECC521 */
