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
#include <stdio.h>
#include "App_Info.h"

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

static unsigned char g_flashReady;
static unsigned char g_fingerprint[9];

/* Set when the last 0x34/0x36/0x37 job downloaded the flash driver (fs) into
 * ITCM.  The APP version policy only makes sense for the APP image, so the
 * dependency check must not run check_version() while fs was the last image. */
static unsigned char g_progImageIsDriver;

static unsigned char BootProg_InitFlash(void)
{
    if (0U != g_flashReady)
    {
        return 0U;
    }

    if (0U != Boot_FlashInit())
    {
        return 1U;
    }

    g_flashReady = 1U;
    return 0U;
}

static unsigned char g_copyState;
static unsigned int g_copyOffset;
static unsigned int g_copyCrc;
static unsigned char g_copyBuf[BOOT_PROG_COPY_CHUNK + BOOT_FLASH_WRITE_UNIT]
    __attribute__((aligned(4)));

/* FF00 progress: one 8 KB sector per call. */
static unsigned char g_eraseActive;
static unsigned int g_eraseAddr;
static unsigned int g_eraseLen;
static unsigned int g_eraseNext;
static unsigned int g_eraseEnd;

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
                                       unsigned int *maxBlock)
{
    if ((0U == size) || (BOOT_PROG_MODE_NONE != g_prog.Mode))
    {
        return (unsigned char)NRC_REQUEST_SEQUENCE_ERROR;
    }

    memset(&g_prog, 0, sizeof(g_prog));
    g_prog.Crc = 0xFFFFFFFFUL;

    /* A new download invalidates any erase/copy state of a previous one. */
    g_copyState = 0U;

    if (0U == addr)
    {
        if (size > BOOT_DRIVER_MAX_SIZE)
        {
            return (unsigned char)NRC_REQUEST_OUT_OF_RANGE;
        }
        g_prog.Mode = BOOT_PROG_MODE_RAM;
        g_prog.Target = (unsigned int)Boot_FlashRamBase();
        g_progImageIsDriver = 1U;

        /* The ITCM image is about to be replaced: run INIT again before the
         * next flash job. */
        g_flashReady = 0U;
    }
    else
    {
        unsigned int target = BootProg_MapAddress(addr);

        if ((0U == BootProg_IsRangeAllowed(target, size)) ||
            (0U != (target & (BOOT_FLASH_WRITE_UNIT - 1U))))
        {
            return (unsigned char)NRC_REQUEST_OUT_OF_RANGE;
        }

        if (0U == Boot_FlashDriverValid())
        {
            return (unsigned char)NRC_CONDITIONS_NOT_CORRECT;
        }

        if (0U != BootProg_InitFlash())
        {
            return (unsigned char)NRC_GENERAL_PROGRAMMING_FAILURE;
        }

        g_prog.Mode = BOOT_PROG_MODE_FLASH;
        g_prog.Target = target;
        g_progImageIsDriver = 0U;
    }

    g_prog.Size = size;
    *maxBlock = BOOT_PROG_MAX_BLOCK;

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
    if ((0U == len) || (len > BOOT_PROG_MAX_BLOCK) ||
        (BOOT_PROG_MODE_NONE == g_prog.Mode) ||
        (0U == data))
    {
        return (unsigned char)NRC_REQUEST_SEQUENCE_ERROR;
    }

    /* ZCANPRO starts the first block of a file block with sequence 0.
     * Accept both 0 and 1 for the first block; later blocks must increment. */
    if ((0U == g_prog.Received) && (0U == seq))
    {
        /* first block, ZCANPRO-style sequence 0 */
    }
    else if (((unsigned char)(g_prog.Seq + 1U)) != seq)
    {
        return (unsigned char)NRC_REQUEST_SEQUENCE_ERROR;
    }

    if ((g_prog.Received + len) > g_prog.Size)
    {
        return (unsigned char)NRC_REQUEST_OUT_OF_RANGE;
    }

    if (BOOT_PROG_MODE_RAM == g_prog.Mode)
    {
        memcpy((unsigned char *)(g_prog.Target + g_prog.Received), data, len);
        g_prog.Received += len;
        g_prog.Crc = Boot_Crc32Update(g_prog.Crc, data, len);
        g_prog.Seq = seq;
    }
    else
    {
        unsigned char oldSeq = g_prog.Seq;
        unsigned int oldReceived = g_prog.Received;
        unsigned int oldBufLen = g_prog.BufLen;
        unsigned int oldCrc = g_prog.Crc;

        if ((g_prog.BufLen + len) > BOOT_PROG_BUF_SIZE)
        {
            return (unsigned char)NRC_REQUEST_SEQUENCE_ERROR;
        }

        memcpy(&g_prog.Buf[g_prog.BufLen], data, len);
        g_prog.BufLen += len;
        g_prog.Received += len;
        g_prog.Crc = Boot_Crc32Update(g_prog.Crc, data, len);
        g_prog.Seq = seq;

        if (0U != BootProg_Flush())
        {
            /* Keep the block context unchanged so the tester can retry the
             * same 0x36 block (the buffer still holds the unsaved bytes). */
            g_prog.Seq = oldSeq;
            g_prog.Received = oldReceived;
            g_prog.BufLen = oldBufLen;
            g_prog.Crc = oldCrc;
            return (unsigned char)NRC_GENERAL_PROGRAMMING_FAILURE;
        }
    }

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

    if (BOOT_PROG_MODE_RAM == g_prog.Mode)
    {
        /* The downloaded driver must be initialised again on the next flash
         * job (and the I-cache must be invalidated). */
        g_flashReady = 0U;
    }
    else
    {
        /* A new APP image invalidates a previous copy progress. */
        g_copyState = 0U;
    }

    *crc = g_prog.Crc ^ 0xFFFFFFFFUL;
    g_prog.Mode = BOOT_PROG_MODE_NONE;
    g_prog.BufLen = 0U;

    return nrc;
}

unsigned char BootProg_EraseStep(unsigned int addr, unsigned int len,
                                 unsigned int *status, unsigned char *more)
{
    unsigned int start;
    unsigned int end;

    *status = 0U;
    *more = 0U;

    if ((0U == len) || (addr > 0x007FFFFFUL) || (len > 0x00800000UL))
    {
        return (unsigned char)NRC_REQUEST_OUT_OF_RANGE;
    }

    /* A different request (or a stale state after a session change) restarts
     * the range.  The DCM calls this function again with the same request
     * while it answers NRC 0x78. */
    if ((0U == g_eraseActive) || (g_eraseAddr != addr) ||
        (g_eraseLen != len))
    {
        start = addr & ~(BOOT_APP_ERASE_UNIT - 1U);
        end   = (addr + len + BOOT_APP_ERASE_UNIT - 1U) &
                ~(BOOT_APP_ERASE_UNIT - 1U);

        if ((end < start) ||
            (0U == BootProg_IsRangeAllowed(start, end - start)))
        {
            g_eraseActive = 0U;
            return (unsigned char)NRC_REQUEST_OUT_OF_RANGE;
        }

        if (0U != BootProg_InitFlash())
        {
            *status = 1U;
            return (unsigned char)NRC_CONDITIONS_NOT_CORRECT;
        }

        g_eraseAddr  = addr;
        g_eraseLen   = len;
        g_eraseNext  = start;
        g_eraseEnd   = end;
        g_eraseActive = 1U;
    }

    /* One 8 KB sector per DCM poll: FF00 then answers 0x78 until the whole
     * requested range has been erased. */
    if (g_eraseNext < g_eraseEnd)
    {
        if (0U != Boot_FlashErase(g_eraseNext, BOOT_APP_ERASE_UNIT))
        {
            g_eraseActive = 0U;
            *status = 1U;
            return (unsigned char)NRC_GENERAL_PROGRAMMING_FAILURE;
        }

        g_eraseNext += BOOT_APP_ERASE_UNIT;
    }

    if (g_eraseNext < g_eraseEnd)
    {
        *more = 1U;
    }
    else
    {
        g_eraseActive = 0U;
    }

    return 0U;
}

/* ---------- 工具函数：严格解析版本号 ---------- */
/* 返回 0 成功；-1 格式错误 */
static int parse_version(const char *buf, size_t len,
                         int *major, int *minor, int *patch)
{
    /* 1. 必须有 '\0' 终止，否则 sscanf 会越界 */
    if (memchr(buf, '\0', len) == NULL) {
        return -1;
    }

    int pos = 0;
    int n = sscanf(buf, "%9d.%9d.%9d%n", major, minor, patch, &pos);
    /* 2. 三段必须都匹配上，且整串被消费完 */
    if (n != 3 || buf[pos] != '\0') {
        return -1;
    }
    return 0;
}

/* ---------- 版本比较：返回 -1/0/1 ---------- */
static int version_cmp(int a1, int b1, int c1,
                       int a2, int b2, int c2)
{
    if (a1 != a2) return (a1 > a2) ? 1 : -1;
    if (b1 != b2) return (b1 > b2) ? 1 : -1;
    if (c1 != c2) return (c1 > c2) ? 1 : -1;
    return 0;
}

/* ---------- 主校验 ---------- */
extern unsigned char BSW_stForceUpdate;
static unsigned char check_version(unsigned int *status)
{
    int major_now = 0, minor_now = 0, patch_now = 0;
    int major_nxt = 0, minor_nxt = 0, patch_nxt = 0;
    char buf[APP_INFO_BLOCK_SIZE];

    /* ECU 一致 */
    if (memcmp((const void *)0x00648120,
               (const void *)0x00400120,
               APP_INFO_BLOCK_SIZE) != 0) {
        *status = 1;
        return (unsigned char)NRC_CONDITIONS_NOT_CORRECT;
    }

    /* 板型号一致 */
    if (memcmp((const void *)0x00648110,
               (const void *)0x00400110,
               APP_INFO_BLOCK_SIZE) != 0) {
        *status = 1;
        return (unsigned char)NRC_CONDITIONS_NOT_CORRECT;
    }

    if(BSW_stForceUpdate != 0)
    {
    	//强制升级，不要求软件版本号一定正向
    	BSW_stForceUpdate = 0;
        *status = 0;
        return 0;
    }

    /* 解析当前版本 */
    memcpy(buf, (const void *)0x00448100, APP_INFO_BLOCK_SIZE);
    if (parse_version(buf, sizeof(buf),
                      &major_now, &minor_now, &patch_now) != 0) {
        *status = 1;
        return (unsigned char)NRC_CONDITIONS_NOT_CORRECT;
    }

    /* 解析新版本 */
    memcpy(buf, (const void *)0x00648100, APP_INFO_BLOCK_SIZE);
    if (parse_version(buf, sizeof(buf),
                      &major_nxt, &minor_nxt, &patch_nxt) != 0) {
        *status = 1;
        return (unsigned char)NRC_CONDITIONS_NOT_CORRECT;
    }

    /* 只允许严格升级，禁止降级和同级互刷 */
    if (version_cmp(major_nxt, minor_nxt, patch_nxt,
                    major_now, minor_now, patch_now) <= 0) {
        *status = 1;
        return (unsigned char)NRC_CONDITIONS_NOT_CORRECT;
    }

    *status = 0;
    return 0;
}

unsigned char BSW_stUpdateVerValid = 0;
unsigned char BootProg_DependencyCheck(unsigned int *status)
{
    *status = 0U;

    /* 刷写 fs（FlashDriver，0x34 地址 0 -> ITCM）时不做版本号校验：
     * fs 镜像里没有 APP 版本信息，正向升级策略只对 APP 生效。 */
    if (0U == g_progImageIsDriver)
    {
        if(check_version(status) != 0)
        {
        	BSW_stUpdateVerValid = 0;
            return (unsigned char)NRC_CONDITIONS_NOT_CORRECT;
        }

        BSW_stUpdateVerValid = 1;
    }
    else
    {
    	BSW_stUpdateVerValid = 0;
    }


    if (0U == Boot_FlashDriverValid())
    {
        *status = 1U;
        return (unsigned char)NRC_CONDITIONS_NOT_CORRECT;
    }

    /* The downloaded image lives in the backup area: check its header. */
    if (BOOT_APP_HEADER_MAGIC != *(volatile unsigned int *)BOOT_APP_BACKUP_BASE)
    {
        *status = 3U;
        return (unsigned char)NRC_CONDITIONS_NOT_CORRECT;
    }

    if (0U == g_prog.AppCrcValid)
    {
        *status = 2U;
        return (unsigned char)NRC_CONDITIONS_NOT_CORRECT;
    }

    if (Boot_Crc32((const unsigned char *)BOOT_APP_BACKUP_BASE, g_prog.AppSize) != g_prog.AppCrc)
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
        (0U != BootProg_InitFlash()))
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
        return (unsigned char)NRC_GENERAL_PROGRAMMING_FAILURE;
    }

    return 0U;
}

unsigned char BootProg_CheckIntegrity(unsigned int expected, unsigned int *status)
{
    *status = 0U;

    if (0U == g_prog.AppCrcValid)
    {
        *status = 1U;
        return 0U;
    }

    if (expected != g_prog.AppCrc)
    {
        *status = 1U;
    }

    return 0U;
}

unsigned char BootProg_WriteFingerprint(const unsigned char *data)
{
    if (0U == data)
    {
        return 1U;
    }

    /* Fee is not integrated yet; keep the interface and the value in RAM. */
    memcpy(g_fingerprint, data, 9U);
    return 0U;
}

unsigned char BootProg_ReadFingerprint(unsigned char *data)
{
    if (0U == data)
    {
        return 1U;
    }

    memcpy(data, g_fingerprint, 9U);
    return 0U;
}

void BootProg_DestroyFlash(void)
{
    Boot_FlashDestroy();
    g_flashReady = 0U;
}






