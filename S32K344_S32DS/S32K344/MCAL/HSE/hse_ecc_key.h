/**
 * @file    hse_ecc_key.h
 * @brief   ECDSA P-521 (SECP521R1) public key verified by UDS 0x27.
 *
 * The tester signs the 0x27 seed with its private key (`id_ecdsa`); the ECU
 * only holds the matching public key and verifies the signature with the HSE.
 */
#ifndef HSE_ECC_KEY_H
#define HSE_ECC_KEY_H

#include <stdint.h>

/** P-521 coordinate length: ceil(521 / 8) = 66 bytes. */
#define HSE_ECC_P521_COORD_LEN  (66U)
/** Public key passed to HSE: X || Y, without the 0x04 prefix. */
#define HSE_ECC_P521_PUB_LEN    (2U * HSE_ECC_P521_COORD_LEN)

extern const uint8_t g_hseEccP521PublicKey[HSE_ECC_P521_PUB_LEN];

#endif /* HSE_ECC_KEY_H */
