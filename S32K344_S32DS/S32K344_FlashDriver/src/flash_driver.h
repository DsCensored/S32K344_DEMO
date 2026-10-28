/*
 * flash_driver.h
 *
 * S32K344 UDS flash driver: the only interface between the bootloader and the
 * RAM/ITCM resident driver image.
 *
 * Image layout (linked at ITCM 0x00000000):
 *   0x0000 : FlashDriver_HeaderType (magic / version / entry / reserved)
 *   0x0010 : .text / .rodata / .data ...
 *
 * Boot usage:
 *   const FlashDriver_HeaderType *hdr = (const FlashDriver_HeaderType *)0x0;
 *   if (FLASHDRIVER_MAGIC == hdr->magic)
 *   {
 *       hdr->entry(FLASHDRIVER_CMD_INIT,   0UL, 0UL, NULL_PTR);
 *       hdr->entry(FLASHDRIVER_CMD_ERASE,  addr, len, NULL_PTR);
 *       hdr->entry(FLASHDRIVER_CMD_WRITE,  addr, len, buf);
 *       hdr->entry(FLASHDRIVER_CMD_DEINIT, 0UL, 0UL, NULL_PTR);
 *   }
 *
 * Rules:
 *   - FLASHDRIVER_CMD_INIT must be called first, and again after a new image
 *     has been downloaded;
 *   - the boot must mask interrupts while the driver is running: the flash
 *     controller stalls instruction fetches from program flash during an
 *     erase/program operation;
 *   - WRITE buffers must be 4-byte aligned and lengths must be a multiple of
 *     FLASHDRIVER_WRITE_PAGE_SIZE (boot RAM buffers are fine).
 */

#ifndef FLASH_DRIVER_H
#define FLASH_DRIVER_H

#include "Std_Types.h"      /* Platform_Types.h + Compiler.h (NULL_PTR) */

/* Header magic: in memory this is 'F' 'L' 'S' 'H' (little endian). */
#define FLASHDRIVER_MAGIC              0x48534C46UL
#define FLASHDRIVER_VERSION            0x00010001UL

/* Commands. */
#define FLASHDRIVER_CMD_INIT           0x01UL   /* clear .bss, init Mem/C40 */
#define FLASHDRIVER_CMD_ERASE          0x02UL   /* erase [addr, addr+len)  */
#define FLASHDRIVER_CMD_WRITE          0x03UL   /* program [addr, addr+len) from buf */
#define FLASHDRIVER_CMD_VERIFY         0x04UL   /* compare [addr, addr+len) with buf */
#define FLASHDRIVER_CMD_DEINIT         0x05UL   /* de-initialise Mem/C40 */

/* Return values. */
#define FLASHDRIVER_OK                 0
#define FLASHDRIVER_E_PARAM           (-1)      /* bad address/length/alignment */
#define FLASHDRIVER_E_SEQUENCE        (-2)      /* INIT not called */
#define FLASHDRIVER_E_NOTIMPL         (-3)      /* command not implemented */
#define FLASHDRIVER_E_FLASH           (-4)      /* flash job failed */
#define FLASHDRIVER_E_VERIFY          (-5)      /* verify mismatch */
#define FLASHDRIVER_E_TIMEOUT         (-6)      /* flash job did not finish */

/*
 * Usable program flash of the S32K344.  The last 176 KB (0x7D4000..0x7FFFFF)
 * are reserved for HSE firmware and are not described by the Mem sector
 * configuration, so they are excluded here as well.
 */
#define FLASHDRIVER_PFLASH_START       0x00400000UL
#define FLASHDRIVER_PFLASH_END         0x007D3FFFUL
#define FLASHDRIVER_SECTOR_SIZE        0x00002000UL      /* 8 KB, minimum erase */
#define FLASHDRIVER_WRITE_PAGE_SIZE    0x00000080UL      /* 128 B, program page */

/* Entry function type used through the header. */
typedef sint32 (*FlashDriver_MainFn)(uint32 cmd, uint32 addr, uint32 len,
                                     uint8 *buf);

/* Header placed at ITCM 0x00000000. */
typedef struct
{
    uint32             magic;       /* FLASHDRIVER_MAGIC */
    uint32             version;     /* FLASHDRIVER_VERSION */
    FlashDriver_MainFn entry;       /* points to main() */
    uint32             reserved;    /* 0 */
} FlashDriver_HeaderType;

/* Driver entry, called by the bootloader through the header. */
int main(uint32 cmd, uint32 addr, uint32 len, uint8 *buf);

#endif /* FLASH_DRIVER_H */
