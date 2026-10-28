/*
 * Boot_Flash.c
 *
 * Calls the flash driver that the tester downloads into ITCM.  The driver
 * implements the commands declared in flash_driver.h (same values here).
 */

#include "Boot_Flash.h"

typedef struct
{
    unsigned int Magic;
    unsigned int Version;
    int (*Entry)(unsigned int cmd, unsigned int addr, unsigned int len,
                 unsigned char *buf);
    unsigned int Reserved;
} Boot_FlashDriverType;

#define BOOT_DRIVER_CMD_INIT    0x01U
#define BOOT_DRIVER_CMD_ERASE   0x02U
#define BOOT_DRIVER_CMD_WRITE   0x03U
#define BOOT_DRIVER_CMD_VERIFY  0x04U
#define BOOT_DRIVER_CMD_DEINIT  0x05U

static const Boot_FlashDriverType *BootFlashHeader(void)
{
    return (const Boot_FlashDriverType *)BOOT_DRIVER_BASE;
}

unsigned char Boot_FlashDriverValid(void)
{
    const Boot_FlashDriverType *drv = BootFlashHeader();
    unsigned int entry;

    if ((BOOT_DRIVER_MAGIC != drv->Magic) ||
        (BOOT_DRIVER_VERSION != drv->Version) ||
        (0 == drv->Entry))
    {
        return 0U;
    }

    entry = (unsigned int)drv->Entry;
    if ((entry >= BOOT_DRIVER_MAX_SIZE) || (0U == (entry & 1U)))
    {
        return 0U;
    }

    return 1U;
}

static int BootFlashCall(unsigned int cmd, unsigned int addr, unsigned int len,
                         unsigned char *buf)
{
    if (0U == Boot_FlashDriverValid())
    {
        return -1;
    }

    return BootFlashHeader()->Entry(cmd, addr, len, buf);
}

unsigned char Boot_FlashInit(void)
{
    return (0 == BootFlashCall(BOOT_DRIVER_CMD_INIT, 0U, 0U, 0)) ? 0U : 1U;
}

unsigned char Boot_FlashDeInit(void)
{
    return (0 == BootFlashCall(BOOT_DRIVER_CMD_DEINIT, 0U, 0U, 0)) ? 0U : 1U;
}

unsigned char Boot_FlashErase(unsigned int addr, unsigned int len)
{
    return (0 == BootFlashCall(BOOT_DRIVER_CMD_ERASE, addr, len, 0)) ? 0U : 1U;
}

unsigned char Boot_FlashWrite(unsigned int addr, const unsigned char *buf, unsigned int len)
{
    return (0 == BootFlashCall(BOOT_DRIVER_CMD_WRITE, addr, len,
                               (unsigned char *)buf)) ? 0U : 1U;
}
