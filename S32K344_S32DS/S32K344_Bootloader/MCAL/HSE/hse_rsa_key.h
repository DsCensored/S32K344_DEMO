/**
 * @file    hse_rsa_key.h
 * @brief   RSA-2048 public key material stored in the bootloader.
 *
 * The modulus/exponent are kept in a separate translation unit so that a new
 * key can be installed by replacing this file only.
 */
#ifndef HSE_RSA_KEY_H
#define HSE_RSA_KEY_H

#include <stdint.h>

/** RSA modulus size in bytes (2048 bits). */
#define HSE_RSA_MODULUS_SIZE        (256U)
/** RSA public exponent size in bytes (65537 = 0x010001). */
#define HSE_RSA_EXPONENT_SIZE       (3U)

/** Public modulus n, big-endian. */
extern const uint8_t g_hseRsaPublicModulus[HSE_RSA_MODULUS_SIZE];
/** Public exponent e, big-endian. */
extern const uint8_t g_hseRsaPublicExponent[HSE_RSA_EXPONENT_SIZE];

#endif /* HSE_RSA_KEY_H */
