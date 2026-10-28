/*
 * Boot.h
 *
 * Bootloader-side boot flow:
 *   - consumes the APP->Boot programming request (see Boot_Shared.h),
 *   - validates the application image,
 *   - jumps to the application after a timeout,
 *   - provides the standard CRC32 used by the programming services.
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

unsigned char Boot_AppIsValid(void);
void Boot_CaptureMagic(void);
void Boot_Init(void);
void Boot_Poll(void);
void Boot_StayInBoot(void);
void Boot_RequestJumpToApp(void);
void Boot_ProcessJumpRequest(void);
void Boot_TryJumpToApp(void);
void Boot_JumpToApp(void);

/* Standard CRC-32 (IEEE 802.3, reflected, poly 0xEDB88320, init/xor 0xFFFFFFFF). */
unsigned int Boot_Crc32(const unsigned char *data, unsigned int len);
/* Running CRC without the final xor; pass the previous return value as crc. */
unsigned int Boot_Crc32Update(unsigned int crc, const unsigned char *data,
                              unsigned int len);

#endif /* BOOT_H_ */




