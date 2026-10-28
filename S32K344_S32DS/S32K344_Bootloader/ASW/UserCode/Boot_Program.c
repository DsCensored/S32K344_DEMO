/*
 * Boot_Program.c
 *
 * Programming state machine of the bootloader.  All flash accesses go through
 * the RAM-resident flash driver (Boot_Flash.h).
 */

#include "Boot_Program.h"
#include "Boot_Flash.h"
#include "Boot.h"
#include "UDS_CanTp.h"
#include <string.h>

#define BOOT_PROG_MODE_NONE     0U
#define BOOT_PROG_MODE_RAM      1U
#define BOOT_PROG_MODE_FLASH    2U
#define BOOT_PROG_BUF_SIZE      4096U
#define BOOT_FLASH_WRITE_UNIT   128U      /* Mem_43_INFLS write page size */
#define BOOT_APP_ERASE_UNIT     0x2000U

typedef struct
{
    unsigned char Mode;
    unsigned int Target;
    unsigned int Size;
    unsigned int Received;
    unsigned int Crc;                     /* running CRC, without final xor */
    unsigned char Seq;
    unsigned char Buf[BOOT_PROG_BUF_SIZE] __attribute__((aligned(4)));
    unsigned int BufLen;
    unsigned char AppCrcValid;
    unsigned int AppCrc;
    unsigned int AppSize;
} Boot_ProgType;

static Boot_ProgType g_prog;

static unsigned char g_copyState;
static unsigned int g_copyOffset;
static unsigned int g_copyCrc;
static unsigned char g_copyBuf[BOOT_PROG_COPY_CHUNK + BOOT_FLASH_WRITE_UNIT]
    __attribute__((aligned(4)));

static unsigned char BootProg_IsRangeAllowed(unsigned int addr, unsigned int len)
{
    unsigned int end = addr + len;

    if ((0U == len) || (end < addr))
    {
        return 0U;
    }

    if ((addr >= BOOT_APP_BASE) && (end <= (BOOT_APP_BASE + BOOT_APP_SIZE)))
    {
        return 1U;
    }

    if ((addr >= BOOT_APP_BACKUP_BASE) &&
        (end <= (BOOT_APP_BACKUP_BASE + BOOT_APP_BACKUP_SIZE)))
    {
        return 1U;
    }

    if ((addr >= BOOT_FLAG_SECTOR_BASE) &&
        (end <= (BOOT_FLAG_SECTOR_BASE + BOOT_FLAG_SECTOR_SIZE)))
    {
        return 1U;
    }

    return 0U;
}

static unsigned int BootProg_MapAddress(unsigned int addr)
{
    if ((addr >= BOOT_APP_BASE) && (addr < (BOOT_APP_BASE + BOOT_APP_SIZE)))
    {
        return BOOT_APP_BACKUP_BASE + (addr - BOOT_APP_BASE);
    }

    return addr;
}

unsigned char BootProg_RequestDownload(unsigned int addr, unsigned int size,
                                       unsigned char *maxBlock)
{
    if ((0U == size) || (BOOT_PROG_MODE_NONE != g_prog.Mode))
    {
        return (unsigned char)NRC_REQUEST_SEQUENCE_ERROR;
    }

    memset(&g_prog, 0, sizeof(g_prog));
    g_prog.Crc = 0xFFFFFFFFUL;

    if (0U == addr)
    {
        if (size > BOOT_DRIVER_MAX_SIZE)
        {
            return (unsigned char)NRC_REQUEST_OUT_OF_RANGE;
        }
        g_prog.Mode = BOOT_PROG_MODE_RAM;
        g_prog.Target = BOOT_DRIVER_BASE;
    }
    else
    {
        unsigned int target = BootProg_MapAddress(addr);

        if ((0U == BootProg_IsRangeAllowed(target, size)) ||
            (0U != (target & (BOOT_FLASH_WRITE_UNIT - 1U))))
        {
            return (unsigned char)NRC_REQUEST_OUT_OF_RANGE;
        }

        g_prog.Mode = BOOT_PROG_MODE_FLASH;
        g_prog.Target = target;
    }

    g_prog.Size = size;
    *maxBlock = (unsigned char)(BOOT_PROG_MAX_BLOCK >> 8U);

    return 0U;
}

static unsigned char BootProg_Flush(void)
{
    unsigned int aligned = g_prog.BufLen & ~(BOOT_FLASH_WRITE_UNIT - 1U);
    unsigned int addr;

    if (0U == aligned)
    {
        return 0U;
    }

    addr = g_prog.Target + g_prog.Received - g_prog.BufLen;
    if (0U != Boot_FlashWrite(addr, g_prog.Buf, aligned))
    {
        return 1U;
    }

    memmove(g_prog.Buf, &g_prog.Buf[aligned], g_prog.BufLen - aligned);
    g_prog.BufLen -= aligned;

    return 0U;
}

unsigned char BootProg_TransferData(unsigned char seq, const unsigned char *data,
                                    unsigned int len)
{
    unsigned int i;

    if ((0U == len) || (len > BOOT_PROG_MAX_BLOCK) ||
        (BOOT_PROG_MODE_NONE == g_prog.Mode) ||
        (0U == data))
    {
        return (unsigned char)NRC_REQUEST_SEQUENCE_ERROR;
    }

    if (((unsigned char)(g_prog.Seq + 1U)) != seq)
    {
        return (unsigned char)NRC_REQUEST_SEQUENCE_ERROR;
    }

    if ((g_prog.Received + len) > g_prog.Size)
    {
        return (unsigned char)NRC_REQUEST_OUT_OF_RANGE;
    }

    g_prog.Crc = Boot_Crc32Update(g_prog.Crc, data, len);
    g_prog.Seq = seq;

    if (BOOT_PROG_MODE_RAM == g_prog.Mode)
    {
        memcpy((unsigned char *)(g_prog.Target + g_prog.Received), data, len);
    }
    else
    {
        if ((g_prog.BufLen + len) > BOOT_PROG_BUF_SIZE)
        {
            return (unsigned char)NRC_REQUEST_SEQUENCE_ERROR;
        }

        memcpy(&g_prog.Buf[g_prog.BufLen], data, len);
        g_prog.BufLen += len;
    }

    g_prog.Received += len;

    if ((BOOT_PROG_MODE_FLASH == g_prog.Mode) && (0U != BootProg_Flush()))
    {
        return (unsigned char)NRC_GENERAL_PROGRAMMING_FAILURE;
    }

    (void)i;
    return 0U;
}

unsigned char BootProg_TransferExit(unsigned int *crc)
{
    unsigned char nrc = 0U;

    if (BOOT_PROG_MODE_NONE == g_prog.Mode)
    {
        return (unsigned char)NRC_REQUEST_SEQUENCE_ERROR;
    }

    if (BOOT_PROG_MODE_RAM == g_prog.Mode)
    {
        if (0U == Boot_FlashDriverValid())
        {
            nrc = (unsigned char)NRC_GENERAL_PROGRAMMING_FAILURE;
        }
    }
    else
    {
        unsigned int aligned = g_prog.BufLen;

        if ((0U != aligned) && (0U != (aligned & (BOOT_FLASH_WRITE_UNIT - 1U))))
        {
            unsigned int pad = BOOT_FLASH_WRITE_UNIT -
                               (aligned & (BOOT_FLASH_WRITE_UNIT - 1U));
            unsigned int addr = g_prog.Target + g_prog.Received - g_prog.BufLen;

            memset(&g_prog.Buf[g_prog.BufLen], 0xFF, pad);
            aligned = g_prog.BufLen + pad;

            if (0U != Boot_FlashWrite(addr, g_prog.Buf, aligned))
            {
                nrc = (unsigned char)NRC_GENERAL_PROGRAMMING_FAILURE;
            }
        }
        else
        {
            if (0U != BootProg_Flush())
            {
                nrc = (unsigned char)NRC_GENERAL_PROGRAMMING_FAILURE;
            }
        }

        if (0U == nrc)
        {
            g_prog.AppCrc = g_prog.Crc ^ 0xFFFFFFFFUL;
            g_prog.AppSize = g_prog.Size;
            g_prog.AppCrcValid = 1U;
        }
    }

    *crc = g_prog.Crc ^ 0xFFFFFFFFUL;
    g_prog.Mode = BOOT_PROG_MODE_NONE;
    g_prog.BufLen = 0U;

    return nrc;
}

unsigned char BootProg_Erase(unsigned int addr, unsigned int len,
                             unsigned int *status)
{
    unsigned int start = addr & ~(BOOT_APP_ERASE_UNIT - 1U);
    unsigned int end = (addr + len + BOOT_APP_ERASE_UNIT - 1U) &
                       ~(BOOT_APP_ERASE_UNIT - 1U);

    *status = 0U;

    if ((0U == len) || (end < start) || (0U == BootProg_IsRangeAllowed(start, end - start)))
    {
        return (unsigned char)NRC_REQUEST_OUT_OF_RANGE;
    }

    if ((0U == Boot_FlashDriverValid()) || (0U != Boot_FlashInit()) ||
        (0U != Boot_FlashErase(start, end - start)))
    {
        *status = 1U;
        return (unsigned char)NRC_GENERAL_PROGRAMMING_FAILURE;
    }

    return 0U;
}

unsigned char BootProg_DependencyCheck(unsigned int *status)
{
    *status = 0U;

    if (0U == Boot_FlashDriverValid())
    {
        *status = 1U;
        return (unsigned char)NRC_CONDITIONS_NOT_CORRECT;
    }

    if (0U == Boot_AppIsValid())
    {
        *status = 3U;
        return (unsigned char)NRC_CONDITIONS_NOT_CORRECT;
    }

    if (0U == g_prog.AppCrcValid)
    {
        *status = 2U;
        return (unsigned char)NRC_CONDITIONS_NOT_CORRECT;
    }

    if (Boot_Crc32((const unsigned char *)BOOT_APP_BASE, g_prog.AppSize) != g_prog.AppCrc)
    {
        *status = 2U;
        return (unsigned char)NRC_CONDITIONS_NOT_CORRECT;
    }

    return 0U;
}

unsigned char BootProg_CopyStep(unsigned int *status, unsigned char *more)
{
    unsigned int chunk;
    unsigned int remaining;

    *status = 0U;
    *more = 0U;

    if ((0U == g_prog.AppCrcValid) || (0U == Boot_FlashDriverValid()) ||
        (0U != Boot_FlashInit()))
    {
        return (unsigned char)NRC_CONDITIONS_NOT_CORRECT;
    }

    if (0U == g_copyState)
    {
        g_copyOffset = 0U;
        g_copyCrc = 0xFFFFFFFFUL;

        if (0U != Boot_FlashErase(BOOT_APP_BASE,
                                  (g_prog.AppSize + BOOT_APP_ERASE_UNIT - 1U) &
                                  ~(BOOT_APP_ERASE_UNIT - 1U)))
        {
            *status = 1U;
            return (unsigned char)NRC_GENERAL_PROGRAMMING_FAILURE;
        }

        g_copyState = 1U;
        *more = 1U;
        return 0U;
    }

    remaining = g_prog.AppSize - g_copyOffset;
    chunk = (remaining > BOOT_PROG_COPY_CHUNK) ? BOOT_PROG_COPY_CHUNK : remaining;

    memcpy(g_copyBuf, (const unsigned char *)(BOOT_APP_BACKUP_BASE + g_copyOffset),
           chunk);
    memset(&g_copyBuf[chunk], 0xFF, BOOT_FLASH_WRITE_UNIT);

    if (0U != Boot_FlashWrite(BOOT_APP_BASE + g_copyOffset, g_copyBuf,
                              (chunk + BOOT_FLASH_WRITE_UNIT - 1U) &
                              ~(BOOT_FLASH_WRITE_UNIT - 1U)))
    {
        *status = 1U;
        g_copyState = 0U;
        return (unsigned char)NRC_GENERAL_PROGRAMMING_FAILURE;
    }

    g_copyCrc = Boot_Crc32Update(g_copyCrc, g_copyBuf, chunk);
    g_copyOffset += chunk;

    if (g_copyOffset < g_prog.AppSize)
    {
        *more = 1U;
        return 0U;
    }

    g_copyState = 0U;
    if ((g_copyCrc ^ 0xFFFFFFFFUL) != g_prog.AppCrc)
    {
        *status = 2U;
    }

    return 0U;
}
