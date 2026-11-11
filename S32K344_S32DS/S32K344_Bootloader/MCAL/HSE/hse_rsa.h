/**
 * @file    hse_rsa.h
 * @brief   HSE RSA-2048 public-key wrapper of the bootloader.
 *
 * The public key from .ssh/S32K344_RSA.pub is stored in the HSE NVM key
 * catalog (group 1, slot 0).  Routine 0x31 01 AA01 formats the catalogs and
 * imports it together with the two AES-128 keys used by UDS 0x27.
 *
 * UDS 0x31 01 02 02 sends the SHA-256 of the downloaded image, encrypted
 * ("signed") with the matching RSA private key on the tester:
 *
 *     request: 31 01 02 02 <RSA-2048 signature:256>
 *     ECU    : RSA public operation -> SHA-256 -> compare with the 0x37 digest
 *
 * Three HSE paths are tried, so the tester may use any of the usual ways of
 * producing the ciphertext:
 *   1. RSASSA-PKCS1-v1_5 signature over the SHA-256 (HSE SIGN / VERIFY),
 *   2. RSAES-PKCS1-v1_5 encryption of the 32-byte digest (HSE RSA CIPHER),
 *   3. raw RSA (no padding) of a PKCS#1 padded block (HSE RSA CIPHER), which
 *      also covers OpenSSL's RSA_private_encrypt().
 */
#ifndef HSE_RSA_H
#define HSE_RSA_H

#include <stdint.h>

#include "hse_rsa_key.h"

/** NVM key catalog slot used for the bootloader RSA public key. */
#define HSE_RSA_KEY_GROUP       (1U)
#define HSE_RSA_KEY_SLOT        (0U)

/** Signature/ciphertext size: one RSA-2048 block. */
#define HSE_RSA_SIGNATURE_SIZE  HSE_RSA_MODULUS_SIZE
/** SHA-256 digest size. */
#define HSE_RSA_HASH_SIZE       (32U)

/** Return codes of HseRsa_VerifyDigest(). */
#define HSE_RSA_OK                  (0U)
#define HSE_RSA_ERR_HASH_MISMATCH   (1U)   /* signature valid, digest differs   */
#define HSE_RSA_ERR_HSE             (2U)   /* no HSE path could verify/decrypt  */
#define HSE_RSA_ERR_PARAM           (3U)   /* bad pointer / signature length    */
#define HSE_RSA_ERR_NOT_READY       (4U)   /* HSE not initialised               */

/** Import the public key into the (already formatted) NVM catalog.
 *  0 = OK; the catalogs must have been formatted first (AA01 does that). */
uint8_t HseRsa_ProgramPublicKey(void);

/** Check that the public key is present and import it if the slot is empty.
 *  The key is imported only once per power cycle. */
uint8_t HseRsa_EnsurePublicKey(void);

/**
 * @brief  Verify a 256-byte RSA block against the expected SHA-256.
 *
 * The block is processed with the HSE public key; the recovered digest is
 * compared with expectedHash.
 *
 * @param  signature    RSA block (HSE_RSA_SIGNATURE_SIZE bytes).
 * @param  signatureLen Length, must be HSE_RSA_SIGNATURE_SIZE.
 * @param  expectedHash SHA-256 of the image (HSE_RSA_HASH_SIZE bytes).
 * @return HSE_RSA_OK, HSE_RSA_ERR_HASH_MISMATCH or one of the error codes.
 */
uint8_t HseRsa_VerifyDigest(const uint8_t *signature, uint16_t signatureLen,
                            const uint8_t expectedHash[HSE_RSA_HASH_SIZE]);

/** Full 32-bit HSE response of the last RSA service (0x55A5AA33 = OK). */
uint32_t HseRsa_GetLastResponse(void);

/** Low byte of the last HSE response (0 = OK). */
uint8_t HseRsa_GetLastError(void);

/** Debug view for J-Link:
 *   [0] import response          [1] key exists (0/1)
 *   [2] RSA PKCS#1 decrypt rsp   [3] RSA no-padding decrypt rsp
 *   [4] SIGN verify response     [5] decrypted length
 *   [6] last VerifyDigest result [7] last raw HSE response */
extern volatile uint32_t g_hseRsaDiag[8];

#endif /* HSE_RSA_H */
