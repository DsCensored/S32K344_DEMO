/**
 * @file    hse_aes.h
 * @brief   HSE AES-128-ECB wrapper used by UDS 0x27 (legacy) and HSE key
 *          catalog formatting.
 *
 *   ECU:  key16 = AES128-ECB(HSE AES key[level], seed16)
 *
 * The AES-128 implementation is compiled only when UDS27_ALGO_AES128 = 1
 * (see hse_crypto_cfg.h).  The catalog formatter is always available because
 * the RSA and ECC keys live in the same NVM key catalog.
 *
 * The two AES keys are stored in the HSE NVM key catalog (group 0, slots 0/1).
 * The bootloader routine 0x31 01 AA01 formats the catalogs and imports the
 * keys; the application can use them directly.
 */
#ifndef HSE_AES_H
#define HSE_AES_H

#include <stdint.h>

#include "hse_crypto_cfg.h"

#define HSE_AES_LEVEL1   (1U)   /* 0x27 01/02 -> key 1 */
#define HSE_AES_LEVEL2   (2U)   /* 0x27 03/04 -> key 2 */

/** 0 = HSE ready */
uint8_t HseAes_Init(void);

/** Format the NVM and RAM key catalogs:
 *  group 0 = AES-128 (2 slots), group 1 = RSA-2048 public key (1 slot),
 *  group 2 = ECC P-521 public key (1 slot).
 *  This is the first step of 0x31 01 AA01; it wipes all NVM keys. */
uint8_t HseKeyCatalog_Format(void);

#if UDS27_ALGO_AES128
/** Import both AES-128 keys into NVM (group 0).  The catalogs must have been
 *  formatted first with HseKeyCatalog_Format(). */
uint8_t HseAes_ProgramKeys(void);

/** Backwards-compatible alias of HseAes_ProgramKeys(). */
uint8_t HseAes_LoadKeys(void);

/** AES-128-ECB encrypt one 16-byte block with key[level].
 *  On the first call the keys are provisioned automatically if they are
 *  missing (so 0x27 also works before AA01 has been run once). */
uint8_t HseAes_EncryptBlock(uint8_t level, const uint8_t in[16], uint8_t out[16]);
#endif /* UDS27_ALGO_AES128 */

/** Low byte of the last HSE response (0 = OK). */
uint8_t HseAes_GetLastError(void);

/** Full 32-bit HSE response of the last service (0x55A5AA33 = OK). */
uint32_t HseAes_GetLastResponse(void);

/** Debug view for J-Link: [0] HSE status, [1] reserved, [2] format,
 *  [3] import L1, [4] import L2, [5] encrypt, [6] last status,
 *  [7] last raw HSE response. */
extern volatile uint32_t g_hseAesDiag[8];

#endif /* HSE_AES_H */
