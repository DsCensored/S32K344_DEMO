/**
 * @file    hse_crypto_cfg.h
 * @brief   UDS 0x27 security access algorithm selection (AES128 / ECC521).
 *
 * Exactly one of the two algorithms must be selected.  The unused
 * implementation is still present in the tree, but it is excluded from the
 * build by these macros so the ECU cannot accidentally keep using AES.
 *
 *   UDS27_ALGO_ECC521 = 1 : 0x27 uses HSE ECDSA P-521 (SECP521R1)
 *   UDS27_ALGO_AES128 = 1 : 0x27 uses HSE AES-128-ECB (legacy behaviour)
 */
#ifndef HSE_CRYPTO_CFG_H
#define HSE_CRYPTO_CFG_H

#define UDS27_ALGO_ECC521       (1)
#define UDS27_ALGO_AES128       (0)

#if ((UDS27_ALGO_ECC521 + UDS27_ALGO_AES128) != 1)
#  error "UDS27_ALGO_ECC521 and UDS27_ALGO_AES128 are mutually exclusive"
#endif

#endif /* HSE_CRYPTO_CFG_H */
