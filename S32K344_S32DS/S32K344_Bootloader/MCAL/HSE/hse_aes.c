/**
 * @file    hse_aes.c
 * @brief   HSE AES-128-ECB for UDS 0x27 seed/key.
 *
 * Algorithm (same as the PC-side sa_demo.dll):
 *     key16 = AES128-ECB(HSE AES key[level], seed16)
 *     level 1: 1b966c61e4255ecc8d69ef4525b42762
 *     level 2: 4d8bba5aad575d2867387fb77f856fec
 *
 * The keys live in the HSE NVM key catalog (group 0, slots 0/1) so they
 * survive a reset.  A blank HSE has INSTALL_OK = 0 and empty key catalogs;
 * in that case the catalogs are formatted first and the keys are imported
 * (this is what routine 0x31 01 AA01 does, exactly like the reference
 * bootloader's HSE_Crypto_Init).  A later 0x27 request just uses the keys.
 *
 * All HSE shared buffers are placed in .non_cacheable_data because HSE
 * cannot access the M7 DTCM (the stack lives there).
 */
#include <string.h>

#include "hse_aes.h"
#include "hse_rsa.h"           /* RSA public key catalog group */
#include "hse_trng.h"
#include "hse_trng_cfg.h"      /* shared section + D-cache helpers */

#include "hse_interface.h"
#include "hse_host.h"
#include "hse_mu.h"

#if defined(__GNUC__)
#define HSE_AES_SHARED  __attribute__((section(".non_cacheable_data"), aligned(16)))
#else
#define HSE_AES_SHARED
#endif

/* NVM catalog layout used by the bootloader.
 * Group 0: two AES-128 slots (0x27 level 1 / level 2).
 * Group 1: one RSA-2048 public key slot (0x31 01 02 02 signature check). */
#define HSE_AES_KEY_GROUP       (0U)
#define HSE_AES_KEY_SLOT_L1     (0U)
#define HSE_AES_KEY_SLOT_L2     (1U)
#define HSE_AES_KEY_HANDLE(slot) \
    ((hseKeyHandle_t)GET_KEY_HANDLE(HSE_KEY_CATALOG_ID_NVM, HSE_AES_KEY_GROUP, (slot)))

static HSE_AES_SHARED hseSrvDescriptor_t     s_desc;
static HSE_AES_SHARED hseKeyInfo_t           s_keyInfo;
static HSE_AES_SHARED hseKeyInfo_t           s_infoOut;
static HSE_AES_SHARED hseKeyGroupCfgEntry_t  s_nvmCatalog[3];
static HSE_AES_SHARED hseKeyGroupCfgEntry_t  s_ramCatalog[3];
static HSE_AES_SHARED uint8_t                s_keyBuf[16];
static HSE_AES_SHARED uint8_t                s_inBuf[16];
static HSE_AES_SHARED uint8_t                s_outBuf[16];

static const uint8_t KEY_L1[16] = {
    0x1b,0x96,0x6c,0x61,0xe4,0x25,0x5e,0xcc,0x8d,0x69,0xef,0x45,0x25,0xb4,0x27,0x62
};
static const uint8_t KEY_L2[16] = {
    0x4d,0x8b,0xba,0x5a,0xad,0x57,0x5d,0x28,0x67,0x38,0x7f,0xb7,0x7f,0x85,0x6f,0xec
};

/* Debug view for the J-Link:
 *   [0] HSE status at init       [1] reserved (0)
 *   [2] format-key-catalogs rsp  [3] import level-1 key response
 *   [4] import level-2 key rsp   [5] last encrypt response
 *   [6] last HSE status          [7] last raw HSE response
 */
volatile uint32_t g_hseAesDiag[8];

static uint8_t  s_channel   = 0xFFU;
static uint8_t  s_keysReady = 0U;
static uint32_t s_lastRsp   = 0U;

/*---------------------------------------------------------------------------
 * Low level helpers
 *-------------------------------------------------------------------------*/
static uint8_t HseAes_Send(void)
{
    hseSrvResponse_t rsp;

    if (0xFFU == s_channel)
    {
        s_channel = (uint8_t)HSE_MU_GetFreeChannel(HSE_TRNG_MU_INSTANCE);
        if (HSE_INVALID_CHANNEL == s_channel)
        {
            s_channel = 0xFFU;
            s_lastRsp = 0xFFFFFFFEUL;
            g_hseAesDiag[7] = s_lastRsp;
            return 0xFEU;
        }
    }

    rsp = HSE_Send(HSE_TRNG_MU_INSTANCE, s_channel, gSyncTxOption, &s_desc);
    s_lastRsp = (uint32_t)rsp;
    g_hseAesDiag[7] = s_lastRsp;

    if (HSE_SRV_RSP_OK != rsp)
    {
        /* Drop the channel so a stuck/odd response cannot poison the next
         * request; the next call obtains a free channel again. */
        s_channel = 0xFFU;
        return (uint8_t)((uint32_t)rsp & 0xFFU);
    }

    return 0U;
}

uint8_t HseAes_Init(void)
{
    uint16_t status = HSE_MU_GetHseStatus(HSE_TRNG_MU_INSTANCE);

    g_hseAesDiag[0] = (uint32_t)status;
    g_hseAesDiag[6] = (uint32_t)status;
    if (0U == (status & (uint16_t)HSE_STATUS_INIT_OK))
    {
        s_lastRsp = 0xFFFFFFE1UL;
        return 0xE1U;                 /* HSE not ready yet */
    }

    return 0U;
}

static uint8_t HseAes_KeyExists(uint8_t slot)
{
    (void)memset(&s_infoOut, 0, sizeof(s_infoOut));
    (void)memset(&s_desc, 0, sizeof(s_desc));
    s_desc.srvId = HSE_SRV_ID_GET_KEY_INFO;
    s_desc.hseSrv.getKeyInfoReq.keyHandle = HSE_AES_KEY_HANDLE(slot);
    s_desc.hseSrv.getKeyInfoReq.pKeyInfo  = (HOST_ADDR)&s_infoOut;

    HseTrng_CacheClean(&s_desc, (uint32_t)sizeof(s_desc));
    HseTrng_CacheClean(&s_infoOut, (uint32_t)sizeof(s_infoOut));
    HseTrng_Dsb();

    (void)HseAes_Send();
    return (HSE_SRV_RSP_OK == (hseSrvResponse_t)s_lastRsp) ? 1U : 0U;
}

static uint8_t HseAes_FormatCatalogs(void)
{
    (void)memset(s_nvmCatalog, 0, sizeof(s_nvmCatalog));
    s_nvmCatalog[0].muMask        = (hseMuMask_t)HSE_ALL_MU_MASK;
    s_nvmCatalog[0].groupOwner    = HSE_KEY_OWNER_CUST;
    s_nvmCatalog[0].keyType       = HSE_KEY_TYPE_AES;
    s_nvmCatalog[0].numOfKeySlots = 2U;
    s_nvmCatalog[0].maxKeyBitLen  = HSE_KEY128_BITS;

    /* Group 1: RSA-2048 public key used by 0x31 01 02 02.  It is imported by
     * HseRsa_ProgramPublicKey() right after this format step (AA01). */
    s_nvmCatalog[1].muMask        = (hseMuMask_t)HSE_ALL_MU_MASK;
    s_nvmCatalog[1].groupOwner    = HSE_KEY_OWNER_CUST;
    s_nvmCatalog[1].keyType       = HSE_KEY_TYPE_RSA_PUB;
    s_nvmCatalog[1].numOfKeySlots = 1U;
    s_nvmCatalog[1].maxKeyBitLen  = HSE_KEY2048_BITS;

    (void)memset(s_ramCatalog, 0, sizeof(s_ramCatalog));
    s_ramCatalog[0].muMask        = (hseMuMask_t)HSE_ALL_MU_MASK;
    s_ramCatalog[0].groupOwner    = HSE_KEY_OWNER_ANY;
    s_ramCatalog[0].keyType       = HSE_KEY_TYPE_AES;
    s_ramCatalog[0].numOfKeySlots = 2U;
    s_ramCatalog[0].maxKeyBitLen  = HSE_KEY128_BITS;

    /* Keep the RAM catalog layout aligned with the NVM one (same groups,
     * owner ANY for RAM).  No RAM RSA key is imported by the bootloader. */
    s_ramCatalog[1].muMask        = (hseMuMask_t)HSE_ALL_MU_MASK;
    s_ramCatalog[1].groupOwner    = HSE_KEY_OWNER_ANY;
    s_ramCatalog[1].keyType       = HSE_KEY_TYPE_RSA_PUB;
    s_ramCatalog[1].numOfKeySlots = 1U;
    s_ramCatalog[1].maxKeyBitLen  = HSE_KEY2048_BITS;

    (void)memset(&s_desc, 0, sizeof(s_desc));
    s_desc.srvId = HSE_SRV_ID_FORMAT_KEY_CATALOGS;
    s_desc.hseSrv.formatKeyCatalogsReq.pNvmKeyCatalogCfg = (HOST_ADDR)s_nvmCatalog;
    s_desc.hseSrv.formatKeyCatalogsReq.pRamKeyCatalogCfg = (HOST_ADDR)s_ramCatalog;

    HseTrng_CacheClean(s_nvmCatalog, (uint32_t)sizeof(s_nvmCatalog));
    HseTrng_CacheClean(s_ramCatalog, (uint32_t)sizeof(s_ramCatalog));
    HseTrng_CacheClean(&s_desc, (uint32_t)sizeof(s_desc));
    HseTrng_Dsb();

    (void)HseAes_Send();
    g_hseAesDiag[2] = s_lastRsp;

    return (HSE_SRV_RSP_OK == (hseSrvResponse_t)s_lastRsp) ? 0U : (uint8_t)(s_lastRsp & 0xFFU);
}

static uint8_t HseAes_ImportOne(uint8_t slot, const uint8_t *pKey)
{
    (void)memset(&s_keyInfo, 0, sizeof(s_keyInfo));
    (void)memcpy(s_keyBuf, pKey, 16U);

    s_keyInfo.keyFlags   = (hseKeyFlags_t)(HSE_KF_USAGE_ENCRYPT | HSE_KF_USAGE_DECRYPT);
    s_keyInfo.keyBitLen  = 128U;
    s_keyInfo.keyCounter = 1UL;      /* NVM anti-replay counter, catalog is fresh */
    s_keyInfo.keyType    = HSE_KEY_TYPE_AES;

    (void)memset(&s_desc, 0, sizeof(s_desc));
    s_desc.srvId = HSE_SRV_ID_IMPORT_KEY;
    s_desc.hseSrv.importKeyReq.targetKeyHandle = HSE_AES_KEY_HANDLE(slot);
    s_desc.hseSrv.importKeyReq.pKeyInfo        = (HOST_ADDR)&s_keyInfo;
    /* Symmetric key material goes in pKey[2]/keyLen[2]; pKey[0] and pKey[1]
     * are the RSA modulus and public exponent. */
    s_desc.hseSrv.importKeyReq.pKey[2]         = (HOST_ADDR)s_keyBuf;
    s_desc.hseSrv.importKeyReq.keyLen[2]       = 16U;
    /* Both optional key handles must be explicitly invalid.  A zeroed
     * descriptor would mean "use NVM key group 0 slot 0 as provision/auth
     * key" and HSE rejects the request with INVALID_ADDR. */
    s_desc.hseSrv.importKeyReq.cipher.cipherKeyHandle        = HSE_INVALID_KEY_HANDLE;
    s_desc.hseSrv.importKeyReq.keyContainer.authKeyHandle     = HSE_INVALID_KEY_HANDLE;

    HseTrng_CacheClean(&s_keyInfo, (uint32_t)sizeof(s_keyInfo));
    HseTrng_CacheClean(s_keyBuf, 16U);
    HseTrng_CacheClean(&s_desc, (uint32_t)sizeof(s_desc));
    HseTrng_Dsb();

    (void)HseAes_Send();
    g_hseAesDiag[(HSE_AES_KEY_SLOT_L1 == slot) ? 3U : 4U] = s_lastRsp;

    return (HSE_SRV_RSP_OK == (hseSrvResponse_t)s_lastRsp) ? 0U : (uint8_t)(s_lastRsp & 0xFFU);
}

/*---------------------------------------------------------------------------
 * Public API
 *-------------------------------------------------------------------------*/
uint8_t HseAes_ProgramKeys(void)
{
    uint8_t err;

    g_hseAesDiag[1] = 0U;
    if (0U != HseAes_Init())
    {
        return 0xE1U;
    }

    /* A blank HSE has no key catalog at all.  Formatting also clears the old
     * NVM keys, which is what the reference AA01 routine does.  (An explicit
     * ERASE_ALL_NVM_KEYS request is rejected while the catalog is unformatted,
     * so format first.) */
    err = HseAes_FormatCatalogs();
    if (0U != err)
    {
        return err;
    }

    err = HseAes_ImportOne(HSE_AES_KEY_SLOT_L1, KEY_L1);
    if (0U != err)
    {
        return err;
    }

    err = HseAes_ImportOne(HSE_AES_KEY_SLOT_L2, KEY_L2);
    if (0U != err)
    {
        return err;
    }

    s_keysReady = 1U;
    return 0U;
}

/* Backwards-compatible name used by the bootloader 0x31 01 AA01 handler. */
uint8_t HseAes_LoadKeys(void)
{
    return HseAes_ProgramKeys();
}

static uint8_t HseAes_EnsureKeys(void)
{
    uint16_t status;

    if (0U != s_keysReady)
    {
        return 0U;
    }

    if (0U != HseAes_Init())
    {
        return 0xE1U;
    }
    status = (uint16_t)g_hseAesDiag[6];

    /* Normal case after AA01 / after the first power-up provisioning:
     * both NVM keys are already there, do not touch the catalog again. */
    if ((0U != HseAes_KeyExists(HSE_AES_KEY_SLOT_L1)) &&
        (0U != HseAes_KeyExists(HSE_AES_KEY_SLOT_L2)))
    {
        s_keysReady = 1U;
        return 0U;
    }

    /* First use on a blank ECU (e.g. 0x27 before AA01): the catalogs are not
     * installed yet, so formatting them is safe and lets 0x27 work. */
    if (0U == (status & (uint16_t)HSE_STATUS_INSTALL_OK))
    {
        return HseAes_ProgramKeys();
    }

    /* Catalogs are already installed but a key is missing.  Do not silently
     * wipe every NVM key from the application; the tester must run AA01. */
    s_lastRsp = 0xFFFFFFE2UL;
    return 0xE2U;
}

uint8_t HseAes_EncryptBlock(uint8_t level, const uint8_t in[16], uint8_t out[16])
{
    hseSymCipherSrv_t *p;
    uint8_t            err;

    if ((0U == level) || (level > 2U) || (0 == in) || (0 == out))
    {
        return 0xF1U;
    }

    err = HseAes_EnsureKeys();
    if (0U != err)
    {
        return err;
    }

    (void)memcpy(s_inBuf, in, 16U);

    (void)memset(&s_desc, 0, sizeof(s_desc));
    s_desc.srvId = HSE_SRV_ID_SYM_CIPHER;
    p = &s_desc.hseSrv.symCipherReq;
    p->accessMode      = HSE_ACCESS_MODE_ONE_PASS;
    p->cipherAlgo      = HSE_CIPHER_ALGO_AES;
    p->cipherBlockMode = HSE_CIPHER_BLOCK_MODE_ECB;
    p->cipherDir       = HSE_CIPHER_DIR_ENCRYPT;
    p->sgtOption       = HSE_SGT_OPTION_NONE;
    p->keyHandle       = HSE_AES_KEY_HANDLE((HSE_AES_LEVEL1 == level) ? HSE_AES_KEY_SLOT_L1 : HSE_AES_KEY_SLOT_L2);
    p->pIV             = (HOST_ADDR)0;      /* not used in ECB mode */
    p->inputLength     = 16U;
    p->pInput          = (HOST_ADDR)s_inBuf;
    p->pOutput         = (HOST_ADDR)s_outBuf;

    HseTrng_CacheClean(s_inBuf, 16U);
    HseTrng_CacheClean(&s_desc, (uint32_t)sizeof(s_desc));
    HseTrng_CacheInvalidate(s_outBuf, 16U);
    HseTrng_Dsb();

    err = HseAes_Send();
    g_hseAesDiag[5] = s_lastRsp;
    if (0U != err)
    {
        return err;
    }

    HseTrng_CacheInvalidate(s_outBuf, 16U);
    HseTrng_Dsb();

    (void)memcpy(out, s_outBuf, 16U);
    return 0U;
}

uint8_t HseAes_GetLastError(void)
{
    return (uint8_t)(s_lastRsp & 0xFFU);
}

uint32_t HseAes_GetLastResponse(void)
{
    return s_lastRsp;
}
