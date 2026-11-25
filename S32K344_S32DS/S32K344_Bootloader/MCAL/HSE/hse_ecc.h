/**
 * @file    hse_ecc.h
 * @brief   HSE ECDSA P-521 (SECP521R1) signature verification for UDS 0x27.
 *
 * Scheme (standard challenge/response, tester signs):
 *   1. HSE TRNG generates a 32-byte seed R; the ECU sends R in plaintext.
 *   2. The tester signs SHA-256(R) with its ECC P-521 private key (id_ecdsa).
 *   3. The tester sends the signature (r,s) back as the 0x27 key.
 *   4. The ECU verifies it with the matching public key (id_ecdsa.pub) stored
 *      in the HSE NVM key catalog (group 2, slot 0).
 *
 * Only the public key is present in the ECU; the private key stays on the
 * tester.  A fresh R for every seed request makes the exchange replay-proof.
 *
 * Routine 0x31 01 AA01 formats the key catalogs and imports the public key.
 */
#ifndef HSE_ECC_H
#define HSE_ECC_H

#include <stdint.h>

/** Fixed 0x27 payload sizes (P-521). */
#define HSE_ECC_RANDOM_LEN      (32U)
#define HSE_ECC_SIG_COMP_LEN    (66U)   /* ceil(521 / 8) */
#define HSE_ECC_SIG_LEN         (2U * HSE_ECC_SIG_COMP_LEN)

/** 0xE3: the signature itself is wrong (verification failed). */
#define HSE_ECC_ERR_VERIFY      (0xE3U)

/** 0 = HSE ready. */
uint8_t HseEcc_Init(void);

/** Import the P-521 public key into the NVM key catalog (group 2, slot 0).
 *  The catalogs must have been formatted first (HseKeyCatalog_Format()).
 *  Called by 0x31 01 AA01. */
uint8_t HseEcc_ProgramPublicKey(void);

/** Verify that the public key is present (0 = ready). */
uint8_t HseEcc_EnsureKey(void);

/** ECDSA-P521/SHA-256 verify.
 *  pMsg is the seed (HSE_ECC_RANDOM_LEN bytes), pR/pS the signature
 *  components as produced by the tester (each <= 66 bytes, big-endian).
 *  Returns 0 = valid, HSE_ECC_ERR_VERIFY = signature invalid, other = HSE
 *  or parameter error. */
uint8_t HseEcc_Verify(const uint8_t *pMsg, uint32_t u32Len,
                      const uint8_t *pR, uint16_t u16RLen,
                      const uint8_t *pS, uint16_t u16SLen);

/** Low byte of the last HSE response (0 = OK). */
uint8_t HseEcc_GetLastError(void);

/** Full 32-bit HSE response of the last service (0x55A5AA33 = OK). */
uint32_t HseEcc_GetLastResponse(void);

/** Debug view for J-Link:
 *  [0] HSE status at init  [1] key-exists result  [2] reserved
 *  [3] import response     [4] verify response    [5] reserved
 *  [6] last error code (0 = OK, 0xE3 = bad signature)
 *  [7] last raw HSE response. */
extern volatile uint32_t g_hseEccDiag[8];

#endif /* HSE_ECC_H */
