/**
 * @file    hse_aes.h
 * @brief   HSE AES-128-ECB wrapper used by UDS 0x27.
 *
 *   ECU:  key16 = AES128-ECB(HSE AES key[level], seed16)
 *
 * The two keys are stored in the HSE NVM key catalog (group 0, slots 0/1).
 * The bootloader routine 0x31 01 AA01 calls HseAes_LoadKeys() to format the
 * catalogs and import the keys; the application can use them directly.
 */
#ifndef HSE_AES_H
#define HSE_AES_H

#include <stdint.h>

#define HSE_AES_LEVEL1   (1U)   /* 0x27 01/02 -> key 1 */
#define HSE_AES_LEVEL2   (2U)   /* 0x27 03/04 -> key 2 */

/** 0 = HSE ready */
uint8_t HseAes_Init(void);

/** Format the key catalogs and import both AES keys into NVM (0x31 01 AA01).
 *  This is intentionally destructive: it is the production provisioning step. */
uint8_t HseAes_ProgramKeys(void);

/** Backwards-compatible alias of HseAes_ProgramKeys(). */
uint8_t HseAes_LoadKeys(void);

/** AES-128-ECB encrypt one 16-byte block with key[level].
 *  On the first call the keys are provisioned automatically if they are
 *  missing (so 0x27 also works before AA01 has been run once). */
uint8_t HseAes_EncryptBlock(uint8_t level, const uint8_t in[16], uint8_t out[16]);

/** Low byte of the last HSE response (0 = OK). */
uint8_t HseAes_GetLastError(void);

/** Full 32-bit HSE response of the last service (0x55A5AA33 = OK). */
uint32_t HseAes_GetLastResponse(void);

/** Debug view for J-Link: [0] HSE status, [1] reserved, [2] format,
 *  [3] import L1, [4] import L2, [5] encrypt, [6] last status,
 *  [7] last raw HSE response. */
extern volatile uint32_t g_hseAesDiag[8];

#endif /* HSE_AES_H */
