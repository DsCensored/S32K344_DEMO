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
 * Start-up check: 0xFF02 stores the SHA-256 and the length of the image that
 * is about to become active in NVM block 3.  Boot_TryJumpToApp() hashes the
 * active area with the HSE and starts the APP only when both values match; on
 * a mismatch (or when the values are missing / cannot be read) the bootloader
 * stays in place.
 *
 * AB swap: 0xFF02 no longer copies the backup area to the run area.  The
 * tester downloads the new BOOT and APP images into the passive partition
 * (0x00600000 / 0x00648000), 0xFF02 verifies them, shifts the block 3 digests
 * and asks the HSE to activate the passive block; the actual swap is done by
 * the HSE at the next reset.  0x31 01 AC C1 performs the same switch without a
 * new download (rollback / BOOT-only update) and exchanges the two digests.
 */

#ifndef BOOT_H_
#define BOOT_H_

/* PFlash layout (must match S32K344_flash.ld of both projects).
 *
 * The device runs the HSE AB swap firmware (HSE_FW 0.2.40.4 is AB swap only),
 * so the 4 MB code flash is split into two 2 MB partitions:
 *
 *   active  partition: BOOT 0x00400000, flag 0x00440000, APP 0x00448000
 *   passive partition: BOOT 0x00600000, flag 0x00640000, APP 0x00648000
 *
 * The M7 always executes from the active addresses; the tester programs the
 * passive window only and HSE_SRV_ID_ACTIVATE_PASSIVE_BLOCK swaps the two
 * partitions at the next reset.  The last 176 KB of each partition
 * (0x005D4000..0x005FFFFF / 0x007D4000..0x007FFFFF) belong to the HSE
 * firmware and are never touched here.
 */
#define BOOT_CODE_BASE          0x00400000UL
#define BOOT_CODE_SIZE          0x00040000UL
#define BOOT_FLAG_SECTOR_BASE   0x00440000UL
#define BOOT_FLAG_SECTOR_SIZE   0x00002000UL
#define BOOT_APP_BASE           0x00448000UL
#define BOOT_APP_SIZE           0x0018C000UL

/* Offset between the active and the passive partition. */
#define BOOT_PASSIVE_OFFSET     0x00200000UL

/* Backup (passive) areas.  They are the images the tester downloads; after
 * the AB swap they appear at the active addresses above. */
#define BOOT_CODE_BACKUP_BASE   (BOOT_CODE_BASE + BOOT_PASSIVE_OFFSET)
#define BOOT_CODE_BACKUP_SIZE   BOOT_CODE_SIZE
#define BOOT_FLAG_BACKUP_BASE   (BOOT_FLAG_SECTOR_BASE + BOOT_PASSIVE_OFFSET)
#define BOOT_FLAG_BACKUP_SIZE   BOOT_FLAG_SECTOR_SIZE
#define BOOT_APP_BACKUP_BASE    (BOOT_APP_BASE + BOOT_PASSIVE_OFFSET)
#define BOOT_APP_BACKUP_SIZE    BOOT_APP_SIZE

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
 *
 * The budget also has to cover the reset that follows an AB swap: HSE FW
 * releases up to 0.2.55 copy the HSE firmware to the new passive partition
 * during that reset, and HSE_STATUS_INIT_OK is set only when the copy has
 * finished (about 1 s).  A normal boot never enters the loop because the probe
 * already succeeds.
 * Budget: ~20000 x 0.12 ms (~2.4 s) on top of the NVM start-up. */
#define BOOT_HSE_INIT_RETRIES   20000U
#define BOOT_HSE_INIT_DELAY     4000U

/* The wait before the clock initialisation (see Boot_TryJumpToApp()) runs at
 * the reset clock (~48 MHz), i.e. ~3x slower per loop than the budget above.
 * 8000 x ~0.33 ms (~2.6 s) covers the HSE firmware backup after a swap. */
#define BOOT_HSE_SWAP_RETRIES   8000U

unsigned char Boot_AppIsValid(void);
void Boot_CaptureMagic(void);
void Boot_Init(void);
void Boot_Poll(void);
void Boot_StayInBoot(void);
void Boot_RequestJumpToApp(void);
void Boot_ProcessJumpRequest(void);
void Boot_TryJumpToApp(void);
void Boot_JumpToApp(void);

/* Image header / vector table check.
 *
 * imageBase is where the image is stored (active or passive address), runBase
 * is the address it was linked for.  The two differ for a passive copy: the
 * APP copied to 0x00648000 still has its vector table linked at 0x0044A000,
 * so the vector table of the passive image lives at 0x0064A000 but contains
 * the linked run addresses.  Returns 1 when the image may be started. */
unsigned char Boot_ImageHeaderValid(unsigned int imageBase, unsigned int runBase,
                                    unsigned int size);

/* Passive partition images, i.e. the ones that become active after the next
 * AB swap.  Used by 0xFF02 / 0x31 01 AC C1 before the swap is requested. */
unsigned char Boot_PassiveBootValid(void);
unsigned char Boot_PassiveAppValid(void);

/* Kept for compatibility.  The AB swap flow does not use it any more: after
 * 0xFF02 the ECU resets (the swap only takes effect at reset), so there is no
 * following 10 01 that could jump to the APP directly. */
void Boot_AppShaConfirm(void);

#endif /* BOOT_H_ */




