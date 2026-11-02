/*
 * Boot_Flash.c
 *
 * The bootloader does not contain any flash programming code.  The tester
 * downloads the flash driver image into ITCM 0x0 (0x34/0x36/0x37) and this
 * module calls the command entry stored in the driver header.
 *
 * The calls run with interrupts masked: while the C40 controller is erasing or
 * programming, instruction fetches from program flash stall.  The driver
 * itself executes from ITCM, so it makes progress; any interrupt taken from
 * the bootloader in program flash would hang.
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

int Boot_FlashLastError;

/* Address 0 is the ITCM on this device, not a NULL pointer.  The base is read
 * through a volatile variable on purpose: when the compiler sees the constant
 * 0 it applies the C rule that dereferencing a NULL pointer is undefined
 * behaviour and replaces the header read with a trap instruction (udf), which
 * is exactly what made the 0x37 service hard-fault. */
static volatile unsigned int BootFlash_Base = BOOT_DRIVER_BASE;

const void *Boot_FlashRamBase(void)
{
    return (const void *)(unsigned int)BootFlash_Base;
}

static const Boot_FlashDriverType *BootFlashHeader(void)
{
    return (const Boot_FlashDriverType *)(unsigned int)BootFlash_Base;
}

/* Make sure the ITCM is enabled before reading the downloaded header. */
static void BootFlash_EnableItcm(void)
{
    *(volatile unsigned int *)0xE000EF90UL |= 1U;   /* ITCMCR.EN */
    __asm volatile ("dsb" ::: "memory");
    __asm volatile ("isb" ::: "memory");
}

/* The image is written to ITCM by the 0x36 handler.  Invalidate the I-cache
 * before the first indirect call, otherwise the instruction fetch may use
 * stale lines from a previous image. */
static void BootFlash_SyncCaches(void)
{
    BootFlash_EnableItcm();

    *(volatile unsigned int *)0xE000EF50UL = 0U;   /* ICIALLU */
    __asm volatile ("dsb" ::: "memory");
    __asm volatile ("isb" ::: "memory");
}

static unsigned int BootFlash_EnterCritical(void)
{
    unsigned int primask;

    __asm volatile ("mrs %0, primask" : "=r" (primask) :: "memory");
    __asm volatile ("cpsid i" ::: "memory");
    return primask;
}

static void BootFlash_ExitCritical(unsigned int primask)
{
    __asm volatile ("msr primask, %0" :: "r" (primask) : "memory");
    __asm volatile ("isb" ::: "memory");
}

unsigned char Boot_FlashDriverValid(void)
{
    const Boot_FlashDriverType *drv;
    unsigned int entry;

    BootFlash_EnableItcm();
    drv = BootFlashHeader();

    if ((BOOT_DRIVER_MAGIC != drv->Magic) ||
        (BOOT_DRIVER_VERSION != drv->Version) ||
        (0U == (unsigned int)drv->Entry))
    {
        return 0U;
    }

    entry = (unsigned int)drv->Entry;
    if ((entry < 0x10U) || (entry >= BOOT_DRIVER_MAX_SIZE) ||
        (0U == (entry & 1U)))
    {
        return 0U;
    }

    return 1U;
}

static int BootFlashCall(unsigned int cmd, unsigned int addr, unsigned int len,
                         unsigned char *buf)
{
    unsigned int primask;
    int ret;

    if (0U == Boot_FlashDriverValid())
    {
        Boot_FlashLastError = -1;
        return -1;
    }

    primask = BootFlash_EnterCritical();
    ret = BootFlashHeader()->Entry(cmd, addr, len, buf);
    BootFlash_ExitCritical(primask);

    Boot_FlashLastError = ret;
    return ret;
}

unsigned char Boot_FlashInit(void)
{
    if (0U == Boot_FlashDriverValid())
    {
        Boot_FlashLastError = -1;
        return 1U;
    }

    /* A new image may have been downloaded since the previous INIT. */
    BootFlash_SyncCaches();

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

unsigned char Boot_FlashWrite(unsigned int addr, const unsigned char *buf,
                              unsigned int len)
{
    return (0 == BootFlashCall(BOOT_DRIVER_CMD_WRITE, addr, len,
                               (unsigned char *)buf)) ? 0U : 1U;
}

unsigned char Boot_FlashVerify(unsigned int addr, const unsigned char *buf,
                               unsigned int len)
{
    return (0 == BootFlashCall(BOOT_DRIVER_CMD_VERIFY, addr, len,
                               (unsigned char *)buf)) ? 0U : 1U;
}
