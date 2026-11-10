/**
 * @file    hse_aes.c
 * @brief   HSE AES-128 ECB + 鐎靛棝鎸滅€电厧鍙嗛敍鍦M key catalog閿? *
 * 娑撱倓閲?AES-128 鐎靛棝鎸滈敍?6 鐎涙濡敍澶涚窗
 *   level 1: 1b966c61e4255ecc8d69ef4525b42762
 *   level 2: 4d8bba5aad575d2867387fb77f856fec
 */
#include <string.h>

#include "hse_aes.h"
#include "hse_trng.h"
#include "hse_trng_cfg.h"  /* shared section, D-cache helpers */

#include "hse_interface.h"
#include "hse_host.h"
#include "hse_mu.h"

#if defined(__GNUC__)
#define HSE_AES_SHARED  __attribute__((section(".non_cacheable_data"), aligned(8)))
#else
#define HSE_AES_SHARED
#endif

/* HSE 閸欘垵顔栭梻顔炬畱閸忓彉闊╅崘鍛摠閿涘牅绗夐懗鑺ユ杹閺?DTCM閿?*/
static HSE_AES_SHARED hseSrvDescriptor_t s_desc;
static HSE_AES_SHARED hseKeyInfo_t       s_keyInfo;
static HSE_AES_SHARED uint8_t            s_keyBuf[16];
static HSE_AES_SHARED uint8_t            s_inBuf[16];
static HSE_AES_SHARED uint8_t            s_outBuf[16];

static const uint8_t KEY_L1[16] = {
    0x1b,0x96,0x6c,0x61,0xe4,0x25,0x5e,0xcc,0x8d,0x69,0xef,0x45,0x25,0xb4,0x27,0x62
};
static const uint8_t KEY_L2[16] = {
    0x4d,0x8b,0xba,0x5a,0xad,0x57,0x5d,0x28,0x67,0x38,0x7f,0xb7,0x7f,0x85,0x6f,0xec
};

#define KEY_HANDLE(slot)  ((hseKeyHandle_t)GET_KEY_HANDLE(HSE_KEY_CATALOG_ID_RAM, 0U, (slot)))

volatile uint32_t g_hseAesDiag[8];
static uint8_t s_loaded = 0U;
static uint8_t s_lastErr = 0U;

static uint8_t HseAes_Send(void)
{
    hseSrvResponse_t rsp = HSE_Send(HSE_TRNG_MU_INSTANCE, (uint8_t)HSE_MU_GetFreeChannel(HSE_TRNG_MU_INSTANCE),
                                    gSyncTxOption, &s_desc);
    s_lastErr = (uint8_t)rsp;
    return (HSE_SRV_RSP_OK == rsp) ? 0U : (uint8_t)rsp;
}

static uint8_t HseAes_ImportOne(uint8_t slot, const uint8_t *pKey)
{
    (void)memset(&s_keyInfo, 0, sizeof(s_keyInfo));
    (void)memcpy(s_keyBuf, pKey, 16U);

    s_keyInfo.keyFlags  = (hseKeyFlags_t)HSE_KF_USAGE_ENCRYPT;
    s_keyInfo.keyBitLen = 128U;
    s_keyInfo.keyType   = HSE_KEY_TYPE_AES;

    (void)memset(&s_desc, 0, sizeof(s_desc));
    s_desc.srvId = HSE_SRV_ID_IMPORT_KEY;
    s_desc.hseSrv.importKeyReq.targetKeyHandle   = KEY_HANDLE(slot);
    s_desc.hseSrv.importKeyReq.pKeyInfo           = (HOST_ADDR)&s_keyInfo;
    s_desc.hseSrv.importKeyReq.pKey[0]            = (HOST_ADDR)s_keyBuf;
    s_desc.hseSrv.importKeyReq.keyLen[0]          = 16U;

    HseTrng_CacheClean(&s_keyInfo, (uint32_t)sizeof(s_keyInfo));
    HseTrng_CacheClean(s_keyBuf, 16U);
    HseTrng_CacheClean(&s_desc, (uint32_t)sizeof(s_desc));
    HseTrng_Dsb();

    return HseAes_Send();
}

uint8_t HseAes_Init(void)
{
    if (0U != HseTrng_Init())
    {
        s_lastErr = 0xEEU;
        return s_lastErr;
    }
    return 0U;
}

uint8_t HseAes_LoadKeys(void)
{
    uint8_t err;

    if (0U != HseAes_Init()) return s_lastErr;

    err = HseAes_ImportOne(0U, KEY_L1);
    g_hseAesDiag[0] = err;
    if (0U != err) { s_loaded = 0U; return err; }

    err = HseAes_ImportOne(1U, KEY_L2);
    g_hseAesDiag[1] = err;
    if (0U != err) { s_loaded = 0U; return err; }

    s_loaded = 1U;
    return 0U;
}

uint8_t HseAes_EncryptBlock(uint8_t level, const uint8_t in[16], uint8_t out[16])
{
    hseSymCipherSrv_t *p;
    uint8_t err;

    if ((0U == level) || (level > 2U) || (0 == in) || (0 == out)) return 0xF1U;

    if (0U == s_loaded)
    {
        err = HseAes_LoadKeys();
        if (0U != err) return err;
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
    p->keyHandle       = KEY_HANDLE((HSE_AES_LEVEL1 == level) ? 0U : 1U);
    p->pIV             = (HOST_ADDR)0;
    p->inputLength     = 16U;
    p->pInput          = (HOST_ADDR)s_inBuf;
    p->pOutput         = (HOST_ADDR)s_outBuf;

    HseTrng_CacheClean(s_inBuf, 16U);
    HseTrng_CacheClean(&s_desc, (uint32_t)sizeof(s_desc));
    HseTrng_Dsb();

    err = HseAes_Send();
    g_hseAesDiag[2] = err;
    if (0U != err) return err;

    HseTrng_CacheInvalidate(s_outBuf, 16U);
    HseTrng_Dsb();

    (void)memcpy(out, s_outBuf, 16U);
    (void)memcpy((void *)&g_hseAesDiag[3], s_outBuf, 16U);
    return 0U;
}

uint8_t HseAes_GetLastError(void)
{
    return s_lastErr;
}
