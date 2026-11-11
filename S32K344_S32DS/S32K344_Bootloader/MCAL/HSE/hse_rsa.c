/**
 * @file    hse_rsa.c
 * @brief   HSE RSA-2048 public-key services for the bootloader.
 *
 * 0x31 01 AA01 installs the public key from hse_rsa_key.c in the HSE NVM key
 * catalog (group 1, slot 0).  UDS 0x31 01 02 02 then passes the 256-byte RSA
 * block sent by the tester; the SHA-256 recovered with the public key is
 * compared with the digest of the last 0x37 download.
 *
 * All HSE shared buffers live in .non_cacheable_data because the HSE is a
 * separate core and cannot access the M7 DTCM (where the stack lives).
 */
#include <string.h>

#include "hse_rsa.h"
#include "hse_aes.h"
#include "hse_trng_cfg.h"      /* shared section + D-cache helpers */

#include "hse_interface.h"
#include "hse_host.h"
#include "hse_mu.h"

#if defined(__GNUC__)
#define HSE_RSA_SHARED  __attribute__((section(".non_cacheable_data"), aligned(16)))
#else
#define HSE_RSA_SHARED
#endif

#define HSE_RSA_KEY_HANDLE \
    ((hseKeyHandle_t)GET_KEY_HANDLE(HSE_KEY_CATALOG_ID_NVM, HSE_RSA_KEY_GROUP, HSE_RSA_KEY_SLOT))

#define HSE_RSA_KEY_BITS        (2048U)

/* ASN.1 DigestInfo prefix of a SHA-256 signature (PKCS#1 v1.5):
 * SEQUENCE(0x31) { SEQUENCE { OID sha256, NULL }, OCTET STRING(32) }. */
static const uint8_t s_sha256DigestInfo[19] =
{
    0x30, 0x31, 0x30, 0x0D, 0x06, 0x09, 0x60, 0x86,
    0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05,
    0x00, 0x04, 0x20
};

static HSE_RSA_SHARED hseSrvDescriptor_t s_desc;
static HSE_RSA_SHARED hseKeyInfo_t       s_keyInfo;
static HSE_RSA_SHARED hseKeyInfo_t       s_infoOut;
static HSE_RSA_SHARED uint8_t            s_modBuf[HSE_RSA_MODULUS_SIZE];
static HSE_RSA_SHARED uint8_t            s_expBuf[8];
static HSE_RSA_SHARED uint8_t            s_inBuf[HSE_RSA_MODULUS_SIZE];
static HSE_RSA_SHARED uint8_t            s_outBuf[HSE_RSA_MODULUS_SIZE];
static HSE_RSA_SHARED uint8_t            s_hashBuf[HSE_RSA_HASH_SIZE];
static HSE_RSA_SHARED uint8_t            s_sigBuf[HSE_RSA_SIGNATURE_SIZE];
static HSE_RSA_SHARED uint32_t           s_outLen;
static HSE_RSA_SHARED uint32_t           s_sigLen[2];

volatile uint32_t g_hseRsaDiag[8];

static uint8_t  s_channel = 0xFFU;
static uint8_t  s_rsaReady = 0U;
static uint32_t s_lastRsp  = 0U;

/*---------------------------------------------------------------------------
 * Low level helpers
 *-------------------------------------------------------------------------*/
static uint8_t HseRsa_Send(void)
{
    hseSrvResponse_t rsp;

    if (0xFFU == s_channel)
    {
        s_channel = (uint8_t)HSE_MU_GetFreeChannel(HSE_TRNG_MU_INSTANCE);
        if (HSE_INVALID_CHANNEL == s_channel)
        {
            s_channel = 0xFFU;
            s_lastRsp = 0xFFFFFFFEUL;
            g_hseRsaDiag[7] = s_lastRsp;
            return 0xFEU;
        }
    }

    rsp = HSE_Send(HSE_TRNG_MU_INSTANCE, s_channel, gSyncTxOption, &s_desc);
    s_lastRsp = (uint32_t)rsp;
    g_hseRsaDiag[7] = s_lastRsp;

    if (HSE_SRV_RSP_OK != rsp)
    {
        s_channel = 0xFFU;
        return (uint8_t)((uint32_t)rsp & 0xFFU);
    }

    return 0U;
}

static uint8_t HseRsa_KeyExists(void)
{
    (void)memset(&s_infoOut, 0, sizeof(s_infoOut));
    (void)memset(&s_desc, 0, sizeof(s_desc));
    s_desc.srvId = HSE_SRV_ID_GET_KEY_INFO;
    s_desc.hseSrv.getKeyInfoReq.keyHandle = HSE_RSA_KEY_HANDLE;
    s_desc.hseSrv.getKeyInfoReq.pKeyInfo  = (HOST_ADDR)&s_infoOut;

    HseTrng_CacheClean(&s_desc, (uint32_t)sizeof(s_desc));
    HseTrng_CacheClean(&s_infoOut, (uint32_t)sizeof(s_infoOut));
    HseTrng_Dsb();

    (void)HseRsa_Send();
    return (HSE_SRV_RSP_OK == (hseSrvResponse_t)s_lastRsp) ? 1U : 0U;
}

/*---------------------------------------------------------------------------
 * Key installation
 *-------------------------------------------------------------------------*/
uint8_t HseRsa_ProgramPublicKey(void)
{
    if (0U != HseAes_Init())
    {
        return 0xE1U;
    }

    /* HSE reads the key material from host memory: use the non-cacheable
     * staging buffers instead of the flash-resident const arrays. */
    (void)memcpy(s_modBuf, g_hseRsaPublicModulus, HSE_RSA_MODULUS_SIZE);
    (void)memset(s_expBuf, 0, sizeof(s_expBuf));
    (void)memcpy(s_expBuf, g_hseRsaPublicExponent, HSE_RSA_EXPONENT_SIZE);

    (void)memset(&s_keyInfo, 0, sizeof(s_keyInfo));
    s_keyInfo.keyFlags   = (hseKeyFlags_t)(HSE_KF_USAGE_ENCRYPT | HSE_KF_USAGE_VERIFY);
    s_keyInfo.keyBitLen  = (uint16_t)HSE_RSA_KEY_BITS;
    s_keyInfo.keyCounter = 1UL;      /* catalog is fresh after AA01 format */
    s_keyInfo.keyType    = HSE_KEY_TYPE_RSA_PUB;
    s_keyInfo.specific.pubExponentSize = (uint8_t)HSE_RSA_EXPONENT_SIZE;

    (void)memset(&s_desc, 0, sizeof(s_desc));
    s_desc.srvId = HSE_SRV_ID_IMPORT_KEY;
    s_desc.hseSrv.importKeyReq.targetKeyHandle = HSE_RSA_KEY_HANDLE;
    s_desc.hseSrv.importKeyReq.pKeyInfo        = (HOST_ADDR)&s_keyInfo;
    s_desc.hseSrv.importKeyReq.pKey[0]         = (HOST_ADDR)s_modBuf;   /* modulus n  */
    s_desc.hseSrv.importKeyReq.keyLen[0]       = (uint16_t)HSE_RSA_MODULUS_SIZE;
    s_desc.hseSrv.importKeyReq.pKey[1]         = (HOST_ADDR)s_expBuf;   /* exponent e */
    s_desc.hseSrv.importKeyReq.keyLen[1]       = (uint16_t)HSE_RSA_EXPONENT_SIZE;
    s_desc.hseSrv.importKeyReq.pKey[2]         = (HOST_ADDR)0;
    s_desc.hseSrv.importKeyReq.keyLen[2]       = 0U;
    /* Both optional key handles must be explicitly invalid; a zeroed value
     * would select NVM group 0 slot 0 as provision key. */
    s_desc.hseSrv.importKeyReq.cipher.cipherKeyHandle    = HSE_INVALID_KEY_HANDLE;
    s_desc.hseSrv.importKeyReq.keyContainer.authKeyHandle = HSE_INVALID_KEY_HANDLE;

    HseTrng_CacheClean(&s_keyInfo, (uint32_t)sizeof(s_keyInfo));
    HseTrng_CacheClean(s_modBuf, HSE_RSA_MODULUS_SIZE);
    HseTrng_CacheClean(s_expBuf, (uint32_t)sizeof(s_expBuf));
    HseTrng_CacheClean(&s_desc, (uint32_t)sizeof(s_desc));
    HseTrng_Dsb();

    (void)HseRsa_Send();
    g_hseRsaDiag[0] = s_lastRsp;

    if (HSE_SRV_RSP_OK != (hseSrvResponse_t)s_lastRsp)
    {
        return (uint8_t)(s_lastRsp & 0xFFU);
    }

    s_rsaReady = 1U;
    return 0U;
}

uint8_t HseRsa_EnsurePublicKey(void)
{
    uint8_t err;

    if (0U != s_rsaReady)
    {
        return 0U;
    }

    if (0U != HseAes_Init())
    {
        return 0xE1U;
    }

    /* Normal case after AA01 / after a power cycle: the NVM key is already
     * there, do not touch the (write-protected) slot. */
    g_hseRsaDiag[1] = (uint32_t)HseRsa_KeyExists();
    if (0U != g_hseRsaDiag[1])
    {
        s_rsaReady = 1U;
        return 0U;
    }

    err = HseRsa_ProgramPublicKey();
    if (0U != err)
    {
        return err;
    }

    return 0U;
}

/*---------------------------------------------------------------------------
 * RSA block processing
 *-------------------------------------------------------------------------*/
/* Copy the digest out of a plaintext block.  Accepts either the bare
 * 32-byte SHA-256 or the 51-byte PKCS#1 DigestInfo (standard RSASSA). */
static uint8_t HseRsa_CopyDigest(const uint8_t *pMsg, uint32_t u32Len,
                                 uint8_t pOutHash[HSE_RSA_HASH_SIZE])
{
    if (HSE_RSA_HASH_SIZE == u32Len)
    {
        (void)memcpy(pOutHash, pMsg, HSE_RSA_HASH_SIZE);
        return 0U;
    }

    if (((uint32_t)HSE_RSA_HASH_SIZE + (uint32_t)sizeof(s_sha256DigestInfo)) == u32Len)
    {
        if (0 == memcmp(pMsg, s_sha256DigestInfo, sizeof(s_sha256DigestInfo)))
        {
            (void)memcpy(pOutHash, &pMsg[sizeof(s_sha256DigestInfo)],
                         HSE_RSA_HASH_SIZE);
            return 0U;
        }
    }

    return 1U;
}

/* Remove the PKCS#1 v1.5 padding of a raw (no-padding) RSA result.
 * Both block types are accepted: 0x01 (signature) and 0x02 (encryption).
 *
 * The HSE may return the modular exponentiation result without its leading
 * zero byte (a PKCS#1 block starts with 0x00), so the value is left-aligned
 * into a full 256-byte block first. */
static uint8_t HseRsa_UnpadBlock(const uint8_t *pBlock, uint32_t u32Len,
                                 uint8_t pOutHash[HSE_RSA_HASH_SIZE])
{
    uint8_t block[HSE_RSA_MODULUS_SIZE];
    uint32_t i;
    uint8_t type;

    if ((0U == u32Len) || (u32Len > (uint32_t)HSE_RSA_MODULUS_SIZE))
    {
        return 1U;
    }

    (void)memset(block, 0, sizeof(block));
    (void)memcpy(&block[HSE_RSA_MODULUS_SIZE - u32Len], pBlock, u32Len);

    if (0x00U != block[0])
    {
        return 1U;
    }

    type = block[1];
    if ((0x01U != type) && (0x02U != type))
    {
        return 1U;
    }

    i = 2U;
    while ((i < (uint32_t)HSE_RSA_MODULUS_SIZE) && (0x00U != block[i]))
    {
        if ((0x01U == type) && (0xFFU != block[i]))
        {
            return 1U;      /* type 1 padding must be all 0xFF */
        }
        i++;
    }

    if (i >= (uint32_t)HSE_RSA_MODULUS_SIZE)
    {
        return 1U;          /* no 0x00 separator */
    }

    if ((i - 2U) < 8U)
    {
        return 1U;          /* PS must be at least 8 bytes */
    }

    return HseRsa_CopyDigest(&block[i + 1U],
                             (uint32_t)HSE_RSA_MODULUS_SIZE - (i + 1U),
                             pOutHash);
}

/* RSA cipher service (public operation).  Returns 0 when the HSE accepted the
 * request; the plaintext is left in s_outBuf / s_outLen. */
static uint8_t HseRsa_RsaCipher(hseRsaAlgo_t algo, const uint8_t *pInput,
                                uint16_t u16InputLen)
{
    hseRsaCipherSrv_t *p;

    if ((0 == pInput) || (0U == u16InputLen) ||
        (u16InputLen > (uint16_t)HSE_RSA_MODULUS_SIZE))
    {
        return HSE_RSA_ERR_PARAM;
    }

    (void)memcpy(s_inBuf, pInput, u16InputLen);
    s_outLen = (uint32_t)HSE_RSA_MODULUS_SIZE;

    (void)memset(&s_desc, 0, sizeof(s_desc));
    s_desc.srvId = HSE_SRV_ID_RSA_CIPHER;
    p = &s_desc.hseSrv.rsaCipherReq;
    p->rsaScheme.rsaAlgo = algo;
    p->cipherDir         = HSE_CIPHER_DIR_DECRYPT;
    p->keyHandle         = HSE_RSA_KEY_HANDLE;
    p->inputLength       = (uint32_t)u16InputLen;
    p->pInput            = (HOST_ADDR)s_inBuf;
    p->pOutputLength     = (HOST_ADDR)&s_outLen;
    p->pOutput           = (HOST_ADDR)s_outBuf;

    HseTrng_CacheClean(s_inBuf, u16InputLen);
    HseTrng_CacheClean(&s_outLen, (uint32_t)sizeof(s_outLen));
    HseTrng_CacheClean(&s_desc, (uint32_t)sizeof(s_desc));
    HseTrng_CacheInvalidate(s_outBuf, (uint32_t)sizeof(s_outBuf));
    HseTrng_Dsb();

    (void)HseRsa_Send();

    HseTrng_CacheInvalidate(&s_outLen, (uint32_t)sizeof(s_outLen));
    HseTrng_CacheInvalidate(s_outBuf, (uint32_t)sizeof(s_outBuf));
    HseTrng_Dsb();

    if (HSE_SRV_RSP_OK != (hseSrvResponse_t)s_lastRsp)
    {
        return HSE_RSA_ERR_HSE;
    }

    if (s_outLen > (uint32_t)HSE_RSA_MODULUS_SIZE)
    {
        s_outLen = (uint32_t)HSE_RSA_MODULUS_SIZE;
    }

    g_hseRsaDiag[5] = s_outLen;
    return HSE_RSA_OK;
}

/* HSE SIGN / VERIFY of a pre-hashed SHA-256 (standard RSASSA-PKCS1-v1_5). */
static uint8_t HseRsa_SignVerify(const uint8_t *pHash, const uint8_t *pSignature,
                                 uint16_t u16SignatureLen)
{
    hseSignSrv_t *p;

    (void)memcpy(s_hashBuf, pHash, HSE_RSA_HASH_SIZE);
    (void)memcpy(s_sigBuf, pSignature, u16SignatureLen);
    s_sigLen[0] = (uint32_t)u16SignatureLen;
    s_sigLen[1] = 0U;

    (void)memset(&s_desc, 0, sizeof(s_desc));
    s_desc.srvId = HSE_SRV_ID_SIGN;
    p = &s_desc.hseSrv.signReq;
    p->accessMode             = HSE_ACCESS_MODE_ONE_PASS;
    p->authDir                = HSE_AUTH_DIR_VERIFY;
    p->bInputIsHashed         = (bool_t)1U;
    p->signScheme.signSch     = HSE_SIGN_RSASSA_PKCS1_V15;
    p->signScheme.sch.rsaPkcs1v15.hashAlgo = HSE_HASH_ALGO_SHA2_256;
    p->keyHandle              = HSE_RSA_KEY_HANDLE;
    p->sgtOption              = HSE_SGT_OPTION_NONE;
    p->inputLength            = (uint32_t)HSE_RSA_HASH_SIZE;
    p->pInput                 = (HOST_ADDR)s_hashBuf;
    p->pSignatureLength[0]    = (HOST_ADDR)&s_sigLen[0];
    p->pSignatureLength[1]    = (HOST_ADDR)0;
    p->pSignature[0]          = (HOST_ADDR)s_sigBuf;
    p->pSignature[1]          = (HOST_ADDR)0;

    HseTrng_CacheClean(s_hashBuf, (uint32_t)sizeof(s_hashBuf));
    HseTrng_CacheClean(s_sigBuf, u16SignatureLen);
    HseTrng_CacheClean(s_sigLen, (uint32_t)sizeof(s_sigLen));
    HseTrng_CacheClean(&s_desc, (uint32_t)sizeof(s_desc));
    HseTrng_Dsb();

    (void)HseRsa_Send();
    g_hseRsaDiag[4] = s_lastRsp;

    if (HSE_SRV_RSP_OK == (hseSrvResponse_t)s_lastRsp)
    {
        return HSE_RSA_OK;
    }

    if (HSE_SRV_RSP_VERIFY_FAILED == (hseSrvResponse_t)s_lastRsp)
    {
        return HSE_RSA_ERR_HASH_MISMATCH;
    }

    return HSE_RSA_ERR_HSE;
}

/*---------------------------------------------------------------------------
 * Public API
 *-------------------------------------------------------------------------*/
uint8_t HseRsa_VerifyDigest(const uint8_t *pSignature, uint16_t u16SignatureLen,
                            const uint8_t pExpectedHash[HSE_RSA_HASH_SIZE])
{
    uint8_t hash[HSE_RSA_HASH_SIZE];
    uint8_t err;

    g_hseRsaDiag[6] = 0xFFFFFFFFUL;

    if ((0 == pSignature) || (0 == pExpectedHash) ||
        ((uint16_t)HSE_RSA_SIGNATURE_SIZE != u16SignatureLen))
    {
        g_hseRsaDiag[6] = (uint32_t)HSE_RSA_ERR_PARAM;
        return HSE_RSA_ERR_PARAM;
    }

    err = HseRsa_EnsurePublicKey();
    if (0U != err)
    {
        g_hseRsaDiag[6] = (uint32_t)HSE_RSA_ERR_HSE;
        return HSE_RSA_ERR_HSE;
    }

    /* 1) Tester encrypted the bare SHA-256 with RSAES-PKCS1-v1_5.  This is
     *    the literal "decrypt the digest with the public key" path. */
    if (HSE_RSA_OK == HseRsa_RsaCipher(HSE_RSA_ALGO_RSAES_PKCS1_V15,
                                       pSignature, u16SignatureLen))
    {
        g_hseRsaDiag[2] = s_lastRsp;

        if (0U == HseRsa_CopyDigest(s_outBuf, s_outLen, hash))
        {
            if (0 == memcmp(hash, pExpectedHash, HSE_RSA_HASH_SIZE))
            {
                g_hseRsaDiag[6] = (uint32_t)HSE_RSA_OK;
                return HSE_RSA_OK;
            }

            g_hseRsaDiag[6] = (uint32_t)HSE_RSA_ERR_HASH_MISMATCH;
            return HSE_RSA_ERR_HASH_MISMATCH;
        }
    }
    else
    {
        g_hseRsaDiag[2] = s_lastRsp;
    }

    /* 2) Raw RSA block (OpenSSL RSA_private_encrypt / RSA_NO_PADDING).
     *    The HSE returns the whole modular exponentiation result and the
     *    PKCS#1 padding is removed here. */
    if (HSE_RSA_OK == HseRsa_RsaCipher(HSE_RSA_ALGO_NO_PADDING,
                                       pSignature, u16SignatureLen))
    {
        g_hseRsaDiag[3] = s_lastRsp;

        if (0U == HseRsa_UnpadBlock(s_outBuf, s_outLen, hash))
        {
            if (0 == memcmp(hash, pExpectedHash, HSE_RSA_HASH_SIZE))
            {
                g_hseRsaDiag[6] = (uint32_t)HSE_RSA_OK;
                return HSE_RSA_OK;
            }

            g_hseRsaDiag[6] = (uint32_t)HSE_RSA_ERR_HASH_MISMATCH;
            return HSE_RSA_ERR_HASH_MISMATCH;
        }
    }
    else
    {
        g_hseRsaDiag[3] = s_lastRsp;
    }

    /* 3) Standard RSASSA-PKCS1-v1_5 signature.  Needed on HSE configurations
     *    where an RSA public key cannot be used with the RSA cipher service. */
    err = HseRsa_SignVerify(pExpectedHash, pSignature, u16SignatureLen);
    g_hseRsaDiag[6] = (uint32_t)err;
    return err;
}

uint32_t HseRsa_GetLastResponse(void)
{
    return s_lastRsp;
}

uint8_t HseRsa_GetLastError(void)
{
    return (uint8_t)(s_lastRsp & 0xFFU);
}
