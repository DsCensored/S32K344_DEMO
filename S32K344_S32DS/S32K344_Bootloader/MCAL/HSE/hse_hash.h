/**
 * @file    hse_hash.h
 * @brief   HSE SHA-256 wrapper used by the bootloader programming services.
 *
 * Replaces the former software CRC32 of the flashing flow:
 *   - 0x37 RequestTransferExit : SHA-256 of the downloaded image (ITCM / backup
 *     area),
 *   - 0x31 01 02 02            : the tester digest is compared with the one
 *     returned by 0x37,
 *   - 0xFF01 dependency check  : SHA-256 of the backup area read back from
 *     flash,
 *   - 0xFF02 copy              : SHA-256 of the run area read back from flash.
 *
 * Only the streaming mode (START / UPDATE / FINISH) is used, so the image is
 * fed to the HSE in 2 KB pieces and never has to be contiguous in HSE-visible
 * memory.  Streaming hashing of SHA2_256 requires the UPDATE length to be a
 * multiple of 64 bytes (the SHA-256 block size); the API hides this.
 *
 * Reference: S32K3_HSE_DemoExamples .../framework/host_crypto_helper/
 *            hse_host_hash.c (HashDataStreamStart/Update/Finish).
 *
 * All shared buffers live in .non_cacheable_data: the HSE is a separate core
 * and cannot access the M7 DTCM (where the stack is).
 *
 * The public header only depends on <stdint.h> so it can be included by the
 * AUTOSAR-style UDS code without colliding with Std_Types.
 */
#ifndef HSE_HASH_H
#define HSE_HASH_H

#include <stdint.h>

/** Return codes: 0 = success, anything else = failure reason. */
#define HSE_HASH_OK                 (0U)
#define HSE_HASH_ERR_NOT_READY      (1U)   /**< HSE not initialised yet      */
#define HSE_HASH_ERR_NO_CHANNEL     (2U)   /**< no free MU service channel   */
#define HSE_HASH_ERR_PARAM          (3U)   /**< bad pointer / length         */
#define HSE_HASH_ERR_HSE_STATUS     (4U)   /**< HSE rejected the request     */
#define HSE_HASH_ERR_DIGEST_LEN     (5U)   /**< HSE returned a wrong length  */

/** SHA-256 digest size in bytes. */
#define HSE_HASH_SHA256_SIZE        (32U)
/** SHA-256 block size in bytes (streaming UPDATE granularity). */
#define HSE_HASH_SHA256_BLOCK       (64U)

/**
 * @brief  Check that the HSE is up and running (lazy, no side effect).
 * @return HSE_HASH_OK when the hashing service can be used.
 */
uint8_t HseHash_Init(void);

/**
 * @brief  Start a SHA-256 stream (HSE_ACCESS_MODE_START).
 *
 * A previous, unfinished stream on the same stream id is discarded: the new
 * download always starts from a clean hash context.
 *
 * @return HSE_HASH_OK on success.
 */
uint8_t HseHash_Start(void);

/**
 * @brief  Feed data to the running stream (HSE_ACCESS_MODE_UPDATE).
 *
 * @param  pData  Source bytes (may be flash or ITCM; they are copied to a
 *                non-cacheable staging buffer before the HSE reads them).
 * @param  u32Len Number of bytes, must be a multiple of 64.
 * @return HSE_HASH_OK on success.
 */
uint8_t HseHash_Update(const uint8_t *pData, uint32_t u32Len);

/**
 * @brief  Terminate the stream and get the digest (HSE_ACCESS_MODE_FINISH).
 *
 * @param  pData     Optional final bytes (any length, e.g. 0..63).
 * @param  u32Len    Length of pData, 0 is allowed.
 * @param  pDigest   Output, HSE_HASH_SHA256_SIZE bytes.
 * @return HSE_HASH_OK on success.
 */
uint8_t HseHash_Finish(const uint8_t *pData, uint32_t u32Len,
                       uint8_t *pDigest);

/**
 * @brief  Compute the SHA-256 of a memory region in one call.
 *
 * Uses START + UPDATE(s) + FINISH internally; the region can have any length
 * and can live in PFlash, ITCM or SRAM.
 *
 * @param  pData    Region base.
 * @param  u32Len   Region length in bytes (0 allowed, yields the SHA-256 of
 *                  the empty message).
 * @param  pDigest  Output, HSE_HASH_SHA256_SIZE bytes.
 * @return HSE_HASH_OK on success.
 */
uint8_t HseHash_Sha256Region(const uint8_t *pData, uint32_t u32Len,
                             uint8_t *pDigest);

/** Last failure reason (HSE_HASH_xxx, 0 = none), for debugging. */
uint8_t HseHash_GetLastError(void);

/** Full 32-bit HSE response of the last service (0x55A5AA33 = OK). */
uint32_t HseHash_GetLastResponse(void);

/** Debug view for J-Link:
 *   [0] HSE status at init   [1] last raw HSE response
 *   [2] last error code      [3] digest length returned by the HSE
 *   [4] UPDATE request count [5] 0x5A5A4841 ("HASH" marker) */
extern volatile uint32_t g_hseHashDiag[6];

#endif /* HSE_HASH_H */
