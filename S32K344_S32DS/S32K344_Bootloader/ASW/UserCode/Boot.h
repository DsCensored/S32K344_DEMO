/*
 * Boot.h
 *
 * Bootloader-side boot flow:
 *   - consumes the APP->Boot programming request (see Boot_Shared.h),
 *   - validates the application image (header + SHA-256 stored in NVM),
 *   - jumps to the application after a timeout.
 *
 * The integrity checks of the programming services are done with the HSE
 * SHA-256 engine, see Boot_Program.h / hse_hash.h.
 *
 * Start-up check: the 0xFF02 copy stores the SHA-256 and the length of the
 * new image in NVM block 3.  Boot_TryJumpToApp() hashes the run area with the
 * HSE and starts the APP only when both values match; on a mismatch (or when
 * the values are missing / cannot be read) the bootloader stays in place.
 */

#ifndef BOOT_H_
#define BOOT_H_

/* PFlash layout (must match S32K344_flash.ld of both projects). */
#define BOOT_CODE_BASE          0x00400000UL
#define BOOT_CODE_SIZE          0x00040000UL
#define BOOT_FLAG_SECTOR_BASE   0x00440000UL
#define BOOT_FLAG_SECTOR_SIZE   0x00002000UL
#define BOOT_APP_BASE           0x00448000UL
#define BOOT_APP_SIZE           0x0018C000UL
/* Backup area and addresses follow the reference bootloader
 * (LBM_LB3_ADDRESS = 0x00648000, LBM_LB3_SIZE = 0x0018C000). */
#define BOOT_APP_BACKUP_BASE    0x00648000UL
#define BOOT_APP_BACKUP_SIZE    0x0018C000UL

/* 16-byte header inserted at BOOT_APP_BASE by ELF2s19:
 *   [0] magic, [1] version, [2] length/CRC, [3] vector table address. */
#define BOOT_APP_HEADER_MAGIC   0x5AA55AA5UL

/* Time without diagnostics after which the bootloader starts the APP. */
#define BOOT_WAIT_TIMEOUT_MS    5000U

/* HSE start-up handshake used by the SHA-256 check of the APP image.
 *
 * After a power-on the HSE firmware boots in parallel with the M7.  Boot.c
 * probes it once (a single MU status read) *before* the NVM initialisation and
 * only waits for it afterwards, so the Fee/DFlash scan (~10 ms) is used as the
 * HSE boot time and the delay loop covers just the remaining part.
 * Budget: ~200 x 0.12 ms (~25 ms) on top of the NVM start-up. */
#define BOOT_HSE_INIT_RETRIES   200U
#define BOOT_HSE_INIT_DELAY     4000U

unsigned char Boot_AppIsValid(void);
void Boot_CaptureMagic(void);
void Boot_Init(void);
void Boot_Poll(void);
void Boot_StayInBoot(void);
void Boot_RequestJumpToApp(void);
void Boot_ProcessJumpRequest(void);
void Boot_TryJumpToApp(void);
void Boot_JumpToApp(void);

/* Called by the 0xFF02 copy after the run area has been written and its
 * SHA-256 matched the value stored in NVM: the image may be started by a
 * following 10 01 without waiting for the next reset. */
void Boot_AppShaConfirm(void);

#endif /* BOOT_H_ */




