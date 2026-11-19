/*
 * Boot_Program.c
 *
 * Programming state machine of the bootloader.  All flash accesses go through
 * the RAM-resident flash driver (Boot_Flash.h).
 */

#include "Boot_Program.h"
#include "Boot_Flash.h"
#include "Boot.h"
#include "Boot_Shared.h"  /* RAM hand-shake for the reset that swaps the banks */
#include "UDS_CanTp.h"
#include <string.h>
#include <stdio.h>
#include "App_Info.h"
#include "hse_hash.h"     /* HSE SHA-256 used for the image integrity checks */
#include "hse_host.h"     /* HSE_ActivatePassiveBlock() (AB swap)          */
#include "hse_trng_cfg.h" /* HSE_TRNG_MU_INSTANCE                          */
#include "Nvm.h"          /* Nvm_CommitBlock() / NVM_BLOCK_3 */
#include "NvmApp.h"       /* Block3Data layout: kBlock3_Now_SHAdata / _FileSize */

#define BOOT_PROG_MODE_NONE     0U
#define BOOT_PROG_MODE_RAM      1U
#define BOOT_PROG_MODE_FLASH    2U
#define BOOT_PROG_BUF_SIZE      4096U
#define BOOT_FLASH_WRITE_UNIT   128U      /* Mem_43_INFLS write page size */
#define BOOT_APP_ERASE_UNIT     0x2000U

/* Kind of the image of the running download (in the passive partition). */
#define BOOT_PROG_IMG_NONE      0U
#define BOOT_PROG_IMG_APP       1U
#define BOOT_PROG_IMG_BOOT      2U

/* SHA-256 + length of one image, as returned by 0x37.  The two images are
 * tracked independently so a BOOT and an APP download can be combined in one
 * programming session regardless of their order. */
typedef struct
{
    unsigned char Valid;
    unsigned int Size;
    unsigned char Hash[HSE_HASH_SHA256_SIZE];
} BootProg_ImageInfoType;

typedef struct
{
    unsigned char Mode;
    unsigned int Target;               /* mapped address (passive partition) */
    unsigned int Size;
    unsigned int Received;
    unsigned char Seq;
    unsigned char Buf[BOOT_PROG_BUF_SIZE] __attribute__((aligned(4)));
    unsigned int BufLen;
    unsigned char ImageKind;           /* BOOT_PROG_IMG_xxx */
    unsigned char LastHashValid;       /* result of the last 0x37 (APP, BOOT or fs) */
    unsigned char LastHash[HSE_HASH_SHA256_SIZE];
} Boot_ProgType;

static Boot_ProgType g_prog;
static BootProg_ImageInfoType g_appImage;
static BootProg_ImageInfoType g_bootImage;

static unsigned char g_flashReady;
static unsigned char g_fingerprint[9];

/* Set when the last 0x34/0x36/0x37 job downloaded the flash driver (fs) into
 * ITCM.  The APP version policy only makes sense for the APP image, so the
 * dependency check must not run check_version() while fs was the last image. */
static unsigned char g_progImageIsDriver;

/* Set after 0xFF02 / 0x31 01 AC C1 requested the HSE swap.  A second swap
 * before the reset must be refused: two consecutive
 * HSE_SRV_ID_ACTIVATE_PASSIVE_BLOCK requests less than about 1 s apart are
 * known to hang the device. */
static unsigned char g_abSwapDone;

/* Debug view for the J-Link:
 *   [0] last 0xFF02 / 0xACC1 status   [1] last raw HSE response
 *   [2] 1 = 0xFF02, 2 = 0xACC1        [3] 0x41425357 ("ABSW" marker) */
volatile unsigned int g_bootAbSwapDiag[4] = {
    0xFFFFFFFFUL, 0U, 0U, 0x41425357UL
};

/* Set by 0xFF01 only when every dependency check passed; it authorises
 * the 0xFF02 swap.  Defined below. */
extern unsigned char BSW_stUpdateVerValid;

/* Programming counters (defined with the block 3 helpers below). */
static void BootProg_CountProgramSuccess(void);
static void BootProg_CountProgramAttempt(void);

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

/* SHA-256 of [base, base + size), computed by the HSE while streaming the
 * bytes through its non-cacheable staging buffer.  The region may live in
 * PFlash or ITCM; returns 0 on success. */
static unsigned char BootProg_HashRegion(unsigned int base, unsigned int size,
                                         unsigned char *digest)
{
    /* Address 0 is the ITCM, not a NULL pointer (see the 0x37 HardFault note
     * in NVM_DFlash_Fee.md 9.6).  Read the base through a volatile variable
     * so the compiler cannot collapse the constant 0 into a trap. */
    volatile unsigned int baseAddr = base;

    if (0U == HseHash_Sha256Region((const unsigned char *)baseAddr,
                                   size, digest))
    {
        return 0U;
    }

    return 1U;
}

/* FF00 progress: one 8 KB sector per call. */
static unsigned char g_eraseActive;
static unsigned int g_eraseAddr;
static unsigned int g_eraseLen;
static unsigned int g_eraseNext;
static unsigned int g_eraseEnd;

/* Only the passive partition may be erased / programmed: the active partition
 * is the one the bootloader itself is executing from.  Programming the active
 * partition would also be pointless, because the HSE swap would replace it
 * with the passive one at the next reset. */
static unsigned char BootProg_IsRangeAllowed(unsigned int addr, unsigned int len)
{
    unsigned int end = addr + len;

    if ((0U == len) || (end < addr))
    {
        return 0U;
    }

    /* The whole passive partition (BOOT + flag sector + APP, without the HSE
     * firmware at the end) may be erased in one FF00 request. */
    if ((addr >= BOOT_CODE_BACKUP_BASE) &&
        (end <= (BOOT_APP_BACKUP_BASE + BOOT_APP_BACKUP_SIZE)))
    {
        return 1U;
    }

    /* Passive BOOT space: 0x00600000..0x0063FFFF. */
    if ((addr >= BOOT_CODE_BACKUP_BASE) &&
        (end <= (BOOT_CODE_BACKUP_BASE + BOOT_CODE_BACKUP_SIZE)))
    {
        return 1U;
    }

    /* Passive flag sector: 0x00640000..0x00641FFF. */
    if ((addr >= BOOT_FLAG_BACKUP_BASE) &&
        (end <= (BOOT_FLAG_BACKUP_BASE + BOOT_FLAG_BACKUP_SIZE)))
    {
        return 1U;
    }

    /* Passive APP area: 0x00648000..0x007D3FFF.  The last 176 KB of the
     * partition (0x007D4000..0x007FFFFF) belong to the HSE firmware and are
     * therefore not writable. */
    if ((addr >= BOOT_APP_BACKUP_BASE) &&
        (end <= (BOOT_APP_BACKUP_BASE + BOOT_APP_BACKUP_SIZE)))
    {
        return 1U;
    }

    return 0U;
}

/* Translate a run address into the passive copy the tester is programming.
 * The tester keeps sending the linked addresses (BOOT 0x00400000, APP
 * 0x00448000); direct passive addresses are accepted unchanged. */
static unsigned int BootProg_MapAddress(unsigned int addr)
{
    if ((addr >= BOOT_CODE_BASE) && (addr < (BOOT_CODE_BASE + BOOT_CODE_SIZE)))
    {
        return addr + BOOT_PASSIVE_OFFSET;
    }

    if ((addr >= BOOT_FLAG_SECTOR_BASE) &&
        (addr < (BOOT_FLAG_SECTOR_BASE + BOOT_FLAG_SECTOR_SIZE)))
    {
        return addr + BOOT_PASSIVE_OFFSET;
    }

    if ((addr >= BOOT_APP_BASE) && (addr < (BOOT_APP_BASE + BOOT_APP_SIZE)))
    {
        return addr + BOOT_PASSIVE_OFFSET;
    }

    return addr;
}

/* Which image is being downloaded, based on the mapped (passive) address. */
static unsigned char BootProg_ImageKind(unsigned int addr, unsigned int len)
{
    unsigned int end = addr + len;

    if ((addr >= BOOT_CODE_BACKUP_BASE) &&
        (end <= (BOOT_CODE_BACKUP_BASE + BOOT_CODE_BACKUP_SIZE)))
    {
        return BOOT_PROG_IMG_BOOT;
    }

    if ((addr >= BOOT_APP_BACKUP_BASE) &&
        (end <= (BOOT_APP_BACKUP_BASE + BOOT_APP_BACKUP_SIZE)))
    {
        return BOOT_PROG_IMG_APP;
    }

    return BOOT_PROG_IMG_NONE;
}

unsigned char BootProg_RequestDownload(unsigned int addr, unsigned int size,
                                       unsigned int *maxBlock)
{
    if ((0U == size) || (BOOT_PROG_MODE_NONE != g_prog.Mode))
    {
        return (unsigned char)NRC_REQUEST_SEQUENCE_ERROR;
    }

    memset(&g_prog, 0, sizeof(g_prog));

    /* A new download invalidates the dependency check of the previous image.
     * The SHA-256 of the other image (BOOT vs APP) is kept, so a BOOT and an
     * APP download can be combined in one session. */
    BSW_stUpdateVerValid = 0U;

    if (0U == addr)
    {
        if (size > BOOT_DRIVER_MAX_SIZE)
        {
            return (unsigned char)NRC_REQUEST_OUT_OF_RANGE;
        }
        g_prog.Mode = BOOT_PROG_MODE_RAM;
        g_prog.Target = (unsigned int)Boot_FlashRamBase();
        g_prog.ImageKind = BOOT_PROG_IMG_NONE;
        g_progImageIsDriver = 1U;

        /* The ITCM image is about to be replaced: run INIT again before the
         * next flash job. */
        g_flashReady = 0U;
    }
    else
    {
        unsigned int target = BootProg_MapAddress(addr);
        unsigned char kind = BootProg_ImageKind(target, size);

        if ((0U == BootProg_IsRangeAllowed(target, size)) ||
            (BOOT_PROG_IMG_NONE == kind) ||
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
        g_prog.ImageKind = kind;
        g_progImageIsDriver = 0U;

        /* A new image of this kind replaces the previous one. */
        if (BOOT_PROG_IMG_BOOT == kind)
        {
            memset(&g_bootImage, 0, sizeof(g_bootImage));
        }
        else
        {
            memset(&g_appImage, 0, sizeof(g_appImage));

            /* Programming attempt of the APP: kBlock3_program_retry_cnt++.
             * Counted when the request is accepted, i.e. the programming
             * actually starts (a request rejected above is not an attempt of
             * this ECU). */
            BootProg_CountProgramAttempt();
        }
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
        g_prog.Seq = seq;
    }
    else
    {
        unsigned char oldSeq = g_prog.Seq;
        unsigned int oldReceived = g_prog.Received;
        unsigned int oldBufLen = g_prog.BufLen;

        if ((g_prog.BufLen + len) > BOOT_PROG_BUF_SIZE)
        {
            return (unsigned char)NRC_REQUEST_SEQUENCE_ERROR;
        }

        memcpy(&g_prog.Buf[g_prog.BufLen], data, len);
        g_prog.BufLen += len;
        g_prog.Received += len;
        g_prog.Seq = seq;

        if (0U != BootProg_Flush())
        {
            /* Keep the block context unchanged so the tester can retry the
             * same 0x36 block (the buffer still holds the unsaved bytes). */
            g_prog.Seq = oldSeq;
            g_prog.Received = oldReceived;
            g_prog.BufLen = oldBufLen;
            return (unsigned char)NRC_GENERAL_PROGRAMMING_FAILURE;
        }
    }

    return 0U;
}

unsigned char BootProg_TransferExit(unsigned char *hash)
{
    unsigned char nrc = 0U;
    unsigned char digest[HSE_HASH_SHA256_SIZE];
    unsigned int imageBase = 0U;

    if ((BOOT_PROG_MODE_NONE == g_prog.Mode) || (0 == hash))
    {
        return (unsigned char)NRC_REQUEST_SEQUENCE_ERROR;
    }

    (void)memset(digest, 0, sizeof(digest));

    if (BOOT_PROG_MODE_RAM == g_prog.Mode)
    {
        if (0U == Boot_FlashDriverValid())
        {
            nrc = (unsigned char)NRC_GENERAL_PROGRAMMING_FAILURE;
        }
        else
        {
            imageBase = (unsigned int)Boot_FlashRamBase();
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

        imageBase = g_prog.Target;
    }

    /* SHA-256 of the image as it is stored: ITCM for the flash driver, the
     * passive partition for the BOOT/APP image.  The 0xFF padding of the last
     * 128-byte write page is outside g_prog.Size and is not hashed. */
    if ((0U == nrc) &&
        (0U != BootProg_HashRegion(imageBase, g_prog.Size, digest)))
    {
        nrc = (unsigned char)NRC_GENERAL_PROGRAMMING_FAILURE;
    }

    if (BOOT_PROG_MODE_RAM == g_prog.Mode)
    {
        /* The downloaded driver must be initialised again on the next flash
         * job (and the I-cache must be invalidated). */
        g_flashReady = 0U;
    }

    if (0U == nrc)
    {
        if (BOOT_PROG_MODE_RAM != g_prog.Mode)
        {
            BootProg_ImageInfoType *image = (BOOT_PROG_IMG_BOOT == g_prog.ImageKind)
                ? &g_bootImage : &g_appImage;

            (void)memcpy(image->Hash, digest, sizeof(image->Hash));
            image->Size = g_prog.Size;
            image->Valid = 1U;
        }

        /* 0x0202 compares the tester digest with this value: keep the SHA-256
         * of the last image even when it was the flash driver (fs). */
        (void)memcpy(g_prog.LastHash, digest, sizeof(g_prog.LastHash));
        g_prog.LastHashValid = 1U;
    }

    (void)memcpy(hash, digest, HSE_HASH_SHA256_SIZE);
    g_prog.Mode = BOOT_PROG_MODE_NONE;
    g_prog.BufLen = 0U;

    return nrc;
}

unsigned char BootProg_EraseStep(unsigned int addr, unsigned int len,
                                 unsigned int *status, unsigned char *more)
{
    unsigned int start;
    unsigned int end;
    unsigned int target;

    *status = BOOT_ST_ERASE_OK;
    *more = 0U;

    if ((0U == len) || (addr > 0x007FFFFFUL) || (len > 0x00800000UL))
    {
        *status = BOOT_ST_ERASE_RANGE;
        return 0U;
    }

    /* FF00 usually uses the same run address as 0x34: erase the passive copy
     * of the requested range. */
    target = BootProg_MapAddress(addr);

    /* A different request (or a stale state after a session change) restarts
     * the range.  The DCM calls this function again with the same request
     * while it answers NRC 0x78. */
    if ((0U == g_eraseActive) || (g_eraseAddr != addr) ||
        (g_eraseLen != len))
    {
        start = target & ~(BOOT_APP_ERASE_UNIT - 1U);
        end   = (target + len + BOOT_APP_ERASE_UNIT - 1U) &
                ~(BOOT_APP_ERASE_UNIT - 1U);

        if ((end < start) ||
            (0U == BootProg_IsRangeAllowed(start, end - start)))
        {
            g_eraseActive = 0U;
            *status = BOOT_ST_ERASE_RANGE;
            return 0U;
        }

        if (0U != BootProg_InitFlash())
        {
            g_eraseActive = 0U;
            *status = BOOT_ST_ERASE_DRIVER;
            return 0U;
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
            *status = BOOT_ST_ERASE_FAIL;
            return 0U;
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
        *status = BOOT_ST_DEP_ECU;
        return 1U;
    }

    /* 板型号一致 */
    if (memcmp((const void *)0x00648110,
               (const void *)0x00400110,
               APP_INFO_BLOCK_SIZE) != 0) {
        *status = BOOT_ST_DEP_HW;
        return 1U;
    }

    if(BSW_stForceUpdate != 0)
    {
    	//强制升级，不要求软件版本号一定正向
    	BSW_stForceUpdate = 0;
        *status = BOOT_ST_DEP_OK;
        return 0;
    }

    /* 首次编程（新板子）：当前 active 区根本没有 APP，不存在"当前版本"，
     * 正向升级策略没有比较对象，直接放行。用镜像头判断（物理事实），
     * 不依赖 NVM。 */
    if (0U == Boot_ImageHeaderValid(BOOT_APP_BASE, BOOT_APP_BASE, BOOT_APP_SIZE))
    {
        *status = BOOT_ST_DEP_OK;
        return 0;
    }

    /* 解析当前版本 */
    memcpy(buf, (const void *)0x00448100, APP_INFO_BLOCK_SIZE);
    if (parse_version(buf, sizeof(buf),
                      &major_now, &minor_now, &patch_now) != 0) {
        *status = BOOT_ST_DEP_VER_NOW;
        return 1U;
    }

    /* 解析新版本 */
    memcpy(buf, (const void *)0x00648100, APP_INFO_BLOCK_SIZE);
    if (parse_version(buf, sizeof(buf),
                      &major_nxt, &minor_nxt, &patch_nxt) != 0) {
        *status = BOOT_ST_DEP_VER_NEW;
        return 1U;
    }

    /* 只允许严格升级，禁止降级和同级互刷 */
    if (version_cmp(major_nxt, minor_nxt, patch_nxt,
                    major_now, minor_now, patch_now) <= 0) {
        *status = BOOT_ST_DEP_VER_DOWN;
        return 1U;
    }

    *status = BOOT_ST_DEP_OK;
    return 0;
}

/* Defined below (NVM block 3 helpers); used by the BOOT-only 0xFF01 path. */
static unsigned char BootProg_Block3Read(unsigned int shaOffset,
                                         unsigned int sizeOffset,
                                         unsigned char *sha,
                                         unsigned int *size);

/* TRUE only when block 3 is readable and *both* APP entries are still in the
 * erased state, i.e. this is a new board on which no APP has ever been
 * programmed.
 *
 * The BOOT-only paths refuse to activate a passive partition whose APP does
 * not match the SHA-256 recorded for it - that protects a running product from
 * activating a half-written APP.  On a new board there is no APP to protect:
 * after the swap the ECU simply stays in the bootloader (Boot_AppIsValid()
 * fails), so the check has nothing to verify and the BOOT-only swap is
 * allowed.  A block 3 that cannot be read keeps the strict behaviour. */
static unsigned char BootProg_NoAppProgrammed(void)
{
    unsigned char sha[HSE_HASH_SHA256_SIZE];
    unsigned int nowSize = 0U;
    unsigned int preSize = 0U;

    if (FALSE != Nvm_GetBlock3Invalid())
    {
        return 0U;
    }

    if ((0U != BootProg_Block3Read(kBlock3_Now_SHAdata, kBlock3_Now_FileSize,
                                   sha, &nowSize)) ||
        (0U != BootProg_Block3Read(kBlock3_Pre_SHAdata, kBlock3_Pre_FileSize,
                                   sha, &preSize)))
    {
        return 0U;
    }

    return ((0xFFFFFFFFUL == nowSize) && (0xFFFFFFFFUL == preSize)) ? 1U : 0U;
}

unsigned char BSW_stUpdateVerValid = 0;
unsigned char BootProg_DependencyCheck(unsigned int *status)
{
    unsigned char digest[HSE_HASH_SHA256_SIZE];

    *status = BOOT_ST_DEP_OK;

    /* 只有本次校验全部通过才允许 0xFF02 切区。 */
    BSW_stUpdateVerValid = 0U;

    if (0U == Boot_FlashDriverValid())
    {
        *status = BOOT_ST_DEP_DRIVER;
        return 0U;
    }

    /* 刷写 fs（FlashDriver，0x34 地址 0 -> ITCM）时不做 APP 校验：
     * fs 镜像里没有 APP 版本信息，正向升级策略只对 APP 生效。 */
    if (0U != g_progImageIsDriver)
    {
        return 0U;
    }

    if (0U != g_appImage.Valid)
    {
        /* New APP: normal forward-only version policy + passive APP check. */
        if (check_version(status) != 0U)
        {
            return 0U;
        }

        /* The downloaded APP image lives in the passive area: check its
         * header.  Its vector table is still linked at 0x0044A000, so the
         * passive copy is found at 0x0064A000 (see Boot_ImageHeaderValid()). */
        if (0U == Boot_ImageHeaderValid(BOOT_APP_BACKUP_BASE, BOOT_APP_BASE,
                                        BOOT_APP_SIZE))
        {
            *status = BOOT_ST_DEP_APP_HEADER;
            return 0U;
        }

        /* Re-read the passive area and hash it again: this also catches a
         * flash programming error that the CAN reception cannot see. */
        if (0U != BootProg_HashRegion(BOOT_APP_BACKUP_BASE, g_appImage.Size,
                                      digest))
        {
            *status = BOOT_ST_DEP_HSE;
            return 0U;
        }

        if (0 != memcmp(digest, g_appImage.Hash, HSE_HASH_SHA256_SIZE))
        {
            *status = BOOT_ST_DEP_APP_HASH;
            return 0U;
        }
    }
    else if (0U != g_bootImage.Valid)
    {
        /* BOOT-only update: the APP in the passive partition is not changed
         * and becomes the active one after the swap.  Verify it against the
         * SHA-256 stored for it by the previous 0xFF02 / 0xACC1
         * (kBlock3_Pre_*).  The APP version policy is skipped: no new APP.
         *
         * New board (no APP programmed anywhere): there is nothing to protect,
         * the BOOT-only swap is allowed - see BootProg_NoAppProgrammed(). */
        if (0U == BootProg_NoAppProgrammed())
        {
            unsigned char preSha[HSE_HASH_SHA256_SIZE];
            unsigned int preSize = 0U;

            if ((0U != BootProg_Block3Read(kBlock3_Pre_SHAdata,
                                           kBlock3_Pre_FileSize,
                                           preSha, &preSize)) ||
                (0U == preSize) || (preSize > BOOT_APP_SIZE))
            {
                *status = BOOT_ST_DEP_PRE_INVALID;
                return 0U;
            }

            if (0U == Boot_ImageHeaderValid(BOOT_APP_BACKUP_BASE, BOOT_APP_BASE,
                                            BOOT_APP_SIZE))
            {
                *status = BOOT_ST_DEP_APP_HEADER;
                return 0U;
            }

            if (0U != BootProg_HashRegion(BOOT_APP_BACKUP_BASE, preSize, digest))
            {
                *status = BOOT_ST_DEP_HSE;
                return 0U;
            }

            if (0 != memcmp(digest, preSha, HSE_HASH_SHA256_SIZE))
            {
                *status = BOOT_ST_DEP_PRE_INVALID;
                return 0U;
            }
        }
    }
    else
    {
        *status = BOOT_ST_DEP_APP_INCOMPLETE;
        return 0U;
    }

    /* A BOOT image downloaded in the same session is verified as well. */
    if (0U != g_bootImage.Valid)
    {
        if (0U == Boot_ImageHeaderValid(BOOT_CODE_BACKUP_BASE, BOOT_CODE_BASE,
                                        BOOT_CODE_BACKUP_SIZE))
        {
            *status = BOOT_ST_DEP_BOOT_HEADER;
            return 0U;
        }

        if (0U != BootProg_HashRegion(BOOT_CODE_BACKUP_BASE, g_bootImage.Size,
                                      digest))
        {
            *status = BOOT_ST_DEP_HSE;
            return 0U;
        }

        if (0 != memcmp(digest, g_bootImage.Hash, HSE_HASH_SHA256_SIZE))
        {
            *status = BOOT_ST_DEP_BOOT_HASH;
            return 0U;
        }
    }

    BSW_stUpdateVerValid = 1U;
    return 0U;
}

/* ---------------------------------------------------------------------------
 * NVM block 3: SHA-256 and length of the two images
 *
 *   kBlock3_Now_SHAdata / kBlock3_Now_FileSize : active image
 *   kBlock3_Pre_SHAdata / kBlock3_Pre_FileSize : passive image
 *
 * The invariant is maintained by the two swap paths below:
 *   - 0xFF02 (a new APP was downloaded): Pre <- Now, Now <- new APP
 *   - 0xACC1 (no new APP):               Pre <- Now, Now <- old Pre
 * so after every AB swap kBlock3_Now_* describes the image that is running.
 * ------------------------------------------------------------------------- */

static unsigned char BootProg_Block3Read(unsigned int shaOffset,
                                         unsigned int sizeOffset,
                                         unsigned char *sha,
                                         unsigned int *size)
{
    unsigned int i;
    unsigned int value = 0U;

    if ((0 == sha) || (0 == size))
    {
        return 1U;
    }

    for (i = 0U; i < HSE_HASH_SHA256_SIZE; i++)
    {
        sha[i] = Nvm_Block3_Read_U8((Block3Data)(shaOffset + i));
    }

    /* Little-endian, the byte order used by Boot.c at start-up. */
    for (i = 0U; i < 4U; i++)
    {
        value |= ((unsigned int)Nvm_Block3_Read_U8((Block3Data)(sizeOffset + i)))
                 << (8U * i);
    }

    *size = value;
    return 0U;
}

static unsigned char BootProg_Block3Write(unsigned int shaOffset,
                                          unsigned int sizeOffset,
                                          const unsigned char *sha,
                                          unsigned int size)
{
    unsigned int i;

    for (i = 0U; i < HSE_HASH_SHA256_SIZE; i++)
    {
        if (E_OK != Nvm_Block3_Write_U8((Block3Data)(shaOffset + i), sha[i]))
        {
            return 1U;
        }
    }

    for (i = 0U; i < 4U; i++)
    {
        if (E_OK != Nvm_Block3_Write_U8((Block3Data)(sizeOffset + i),
                                        (uint8)((size >> (8U * i)) & 0xFFU)))
        {
            return 1U;
        }
    }

    return 0U;
}

/* One image entry of block 3: SHA-256 + length. */
typedef struct
{
    unsigned char Sha[HSE_HASH_SHA256_SIZE];
    unsigned int  Size;
} BootProg_Block3EntryType;

/* All four entries, used to undo a refused HSE swap. */
typedef struct
{
    BootProg_Block3EntryType AppNow;    /* active  APP (running)            */
    BootProg_Block3EntryType AppPre;    /* passive APP (becomes active)     */
    BootProg_Block3EntryType BootNow;   /* active  BOOT (running)           */
    BootProg_Block3EntryType BootPre;   /* passive BOOT (becomes active)    */
} BootProg_Block3StateType;

static void BootProg_Block3StateRead(BootProg_Block3StateType *state)
{
    (void)BootProg_Block3Read(kBlock3_Now_SHAdata, kBlock3_Now_FileSize,
                              state->AppNow.Sha, &state->AppNow.Size);
    (void)BootProg_Block3Read(kBlock3_Pre_SHAdata, kBlock3_Pre_FileSize,
                              state->AppPre.Sha, &state->AppPre.Size);
    (void)BootProg_Block3Read(kBlock3_NowBootSHAdata, kBlock3_NowBootSize,
                              state->BootNow.Sha, &state->BootNow.Size);
    (void)BootProg_Block3Read(kBlock3_PreBootSHAdata, kBlock3_PreBootSize,
                              state->BootPre.Sha, &state->BootPre.Size);
}

static void BootProg_Block3StateWrite(const BootProg_Block3StateType *state)
{
    (void)BootProg_Block3Write(kBlock3_Now_SHAdata, kBlock3_Now_FileSize,
                               state->AppNow.Sha, state->AppNow.Size);
    (void)BootProg_Block3Write(kBlock3_Pre_SHAdata, kBlock3_Pre_FileSize,
                               state->AppPre.Sha, state->AppPre.Size);
    (void)BootProg_Block3Write(kBlock3_NowBootSHAdata, kBlock3_NowBootSize,
                               state->BootNow.Sha, state->BootNow.Size);
    (void)BootProg_Block3Write(kBlock3_PreBootSHAdata, kBlock3_PreBootSize,
                               state->BootPre.Sha, state->BootPre.Size);
    (void)Nvm_UserBlock_Polling_10ms();
}

/* Shift one image pair: Pre <- old Now, Now <- newSha/newSize.
 * Not committed here (NvmApp accepts only one pending block, the caller
 * commits once after both pairs). */
static unsigned char BootProg_Block3ShiftPair(unsigned int nowShaOffset,
                                              unsigned int nowSizeOffset,
                                              unsigned int preShaOffset,
                                              unsigned int preSizeOffset,
                                              const unsigned char *newSha,
                                              unsigned int newSize)
{
    unsigned char oldSha[HSE_HASH_SHA256_SIZE];
    unsigned int oldSize = 0U;

    if (0 == newSha)
    {
        return 1U;
    }

    (void)BootProg_Block3Read(nowShaOffset, nowSizeOffset, oldSha, &oldSize);

    if (0U != BootProg_Block3Write(preShaOffset, preSizeOffset, oldSha, oldSize))
    {
        return 1U;
    }

    if (0U != BootProg_Block3Write(nowShaOffset, nowSizeOffset, newSha, newSize))
    {
        return 1U;
    }

    return 0U;
}

/* Resolve the identity of one BOOT window.
 *
 * Normally the SHA-256/length pair stored in block 3 describes the image.  On
 * a new board the BOOT in the active partition came from the debugger and was
 * never recorded, so the entry holds 0xFF/0xFFFFFFFF.  In that case the whole
 * BOOT window (image + whatever follows it) is hashed as it is: the value
 * describes exactly the bytes that will be in that window after the swap, so
 * Boot_PassiveBootValid() can verify it later with the same length.
 *
 * @return 0 = identity available, 1 = HSE failure */
static unsigned char BootProg_BootIdentity(const BootProg_Block3EntryType *stored,
                                           unsigned int windowBase,
                                           unsigned char *sha, unsigned int *size)
{
    if ((0xFFFFFFFFUL != stored->Size) && (0U != stored->Size) &&
        (stored->Size <= BOOT_CODE_SIZE))
    {
        (void)memcpy(sha, stored->Sha, HSE_HASH_SHA256_SIZE);
        *size = stored->Size;
        return 0U;
    }

    if (0U != BootProg_HashRegion(windowBase, BOOT_CODE_SIZE, sha))
    {
        return 1U;
    }

    *size = BOOT_CODE_SIZE;
    return 0U;
}

/* Bring block 3 to the post-swap state, i.e. Now always describes the image
 * that will be running after the reset.
 *
 * For every pair (APP and BOOT):
 *      Pre <- old Now                        (the image that becomes passive)
 *      Now <- the image that becomes active
 * "The image that becomes active" is the download of this session when one was
 * made for it (g_appImage / g_bootImage carry the 0x37 digest and length),
 * otherwise it is the image that already sits in the passive partition - the
 * shift then degenerates into an exchange of the two entries.
 *
 * @return 0 = block 3 updated and committed, 1 = NVM failure */
static unsigned char BootProg_Block3PrepareSwap(void)
{
    BootProg_Block3StateType before;
    unsigned char failed = 0U;

    BootProg_Block3StateRead(&before);

    /* NvmApp accepts only one block waiting for its DFlash commit: push a
     * pending write of another block (e.g. a DID written earlier in the
     * session) out first. */
    (void)Nvm_UserBlock_Polling_10ms();

    /* APP pair. */
    if (0U != g_appImage.Valid)
    {
        failed |= BootProg_Block3ShiftPair(kBlock3_Now_SHAdata,
                                           kBlock3_Now_FileSize,
                                           kBlock3_Pre_SHAdata,
                                           kBlock3_Pre_FileSize,
                                           g_appImage.Hash, g_appImage.Size);
    }
    else
    {
        failed |= BootProg_Block3ShiftPair(kBlock3_Now_SHAdata,
                                           kBlock3_Now_FileSize,
                                           kBlock3_Pre_SHAdata,
                                           kBlock3_Pre_FileSize,
                                           before.AppPre.Sha, before.AppPre.Size);
    }

    /* BOOT pair.  After the swap "Now" is the BOOT that was in the passive
     * partition, "Pre" the one that was active.  A BOOT downloaded in this
     * session is the new "Now"; otherwise the stored passive identity is used.
     * A side whose identity was never recorded (new board) is derived from the
     * partition itself, see BootProg_BootIdentity(). */
    {
        unsigned char nowSha[HSE_HASH_SHA256_SIZE];
        unsigned char preSha[HSE_HASH_SHA256_SIZE];
        unsigned int nowSize = 0U;
        unsigned int preSize = 0U;

        if (0U != g_bootImage.Valid)
        {
            (void)memcpy(nowSha, g_bootImage.Hash, HSE_HASH_SHA256_SIZE);
            nowSize = g_bootImage.Size;
        }
        else
        {
            failed |= BootProg_BootIdentity(&before.BootPre, BOOT_CODE_BACKUP_BASE,
                                            nowSha, &nowSize);
        }

        /* "Pre" is the BOOT that is active today and becomes passive. */
        failed |= BootProg_BootIdentity(&before.BootNow, BOOT_CODE_BASE,
                                        preSha, &preSize);

        if (0U == failed)
        {
            if (0U != BootProg_Block3Write(kBlock3_PreBootSHAdata,
                                           kBlock3_PreBootSize,
                                           preSha, preSize))
            {
                failed = 1U;
            }
            else if (0U != BootProg_Block3Write(kBlock3_NowBootSHAdata,
                                                kBlock3_NowBootSize,
                                                nowSha, nowSize))
            {
                failed = 1U;
            }
        }
    }

    if (0U != failed)
    {
        return 1U;
    }

    /* Commit synchronously: the values must be in the DFlash before the HSE
     * swap is requested. */
    return (E_OK == Nvm_UserBlock_Polling_10ms()) ? 0U : 1U;
}

/* ---------------------------------------------------------------------------
 * Programming counters, stored in block 3 as little-endian 32-bit values:
 *   kBlock3_program_cnt       : successful programming operations (0xFF02)
 *   kBlock3_program_retry_cnt : APP 0x34 requests (programming attempts)
 * ------------------------------------------------------------------------- */

static unsigned int BootProg_Block3ReadCounter(unsigned int offset)
{
    unsigned int i;
    unsigned int value = 0U;

    for (i = 0U; i < 4U; i++)
    {
        value |= ((unsigned int)Nvm_Block3_Read_U8((Block3Data)(offset + i)))
                 << (8U * i);
    }

    return value;
}

/* Read-modify-write one counter and commit the block.  A counter that was
 * never written reads as 0xFFFFFFFF (erased DFlash) and counts as 0. */
static void BootProg_Block3BumpCounter(unsigned int offset)
{
    unsigned int value = BootProg_Block3ReadCounter(offset);
    unsigned int i;

    if (0xFFFFFFFFUL == value)
    {
        value = 0U;
    }
    value++;

    /* Push a pending write of another block out before touching block 3. */
    (void)Nvm_UserBlock_Polling_10ms();

    for (i = 0U; i < 4U; i++)
    {
        (void)Nvm_Block3_Write_U8((Block3Data)(offset + i),
                                  (uint8)((value >> (8U * i)) & 0xFFU));
    }

    (void)Nvm_UserBlock_Polling_10ms();
}

/* A programming operation completed: the image was written, verified and the
 * HSE swap was accepted. */
static void BootProg_CountProgramSuccess(void)
{
    BootProg_Block3BumpCounter((unsigned int)kBlock3_program_cnt);
}

/* An APP 0x34 (RequestDownload) was accepted: count the attempt. */
static void BootProg_CountProgramAttempt(void)
{
    BootProg_Block3BumpCounter((unsigned int)kBlock3_program_retry_cnt);
}

/* Ask the HSE to swap the active/passive partition at the next reset.
 * @return 0 = accepted, 1 = HSE rejected, 2 = HSE not ready */
static unsigned char BootProg_ActivatePassiveBlock(void)
{
    hseSrvResponse_t response;

    /* 0x37/0xFF01 already use the HSE; the probe fails closed when the HSE is
     * not ready (e.g. still copying its firmware after a previous swap). */
    if (HSE_HASH_OK != HseHash_Init())
    {
        g_bootAbSwapDiag[1] = 0xFFFFFFE1UL;
        return 2U;
    }

    response = HSE_ActivatePassiveBlock(HSE_TRNG_MU_INSTANCE);
    g_bootAbSwapDiag[1] = (unsigned int)response;

    if (HSE_SRV_RSP_OK != response)
    {
        return 1U;
    }

    /* Tell the bootloader that runs after the swap reset that the HSE is
     * still busy copying its own firmware, see Boot_TryJumpToApp(). */
    Boot_AbSwapFlagSet();
    __asm volatile ("dsb" ::: "memory");

    return 0U;
}

/* Update block 3 for the swap that is about to happen, ask the HSE to activate
 * the passive block and undo the update when the HSE refuses it.
 *
 * @return BOOT_ST_AB_OK / BOOT_ST_AB_NVM / BOOT_ST_AB_HSE */
static unsigned int BootProg_RequestSwap(void)
{
    BootProg_Block3StateType before;

    /* Snapshot first: after a refused swap the two pairs have to be exactly as
     * they were, otherwise the next start would refuse to boot the still
     * active APP. */
    BootProg_Block3StateRead(&before);

    if (0U != BootProg_Block3PrepareSwap())
    {
        /* A partial write must not stay behind: without the old entries the
         * next start could refuse to boot the still active APP. */
        BootProg_Block3StateWrite(&before);
        return (unsigned int)BOOT_ST_AB_NVM;
    }

    if (0U != BootProg_ActivatePassiveBlock())
    {
        BootProg_Block3StateWrite(&before);
        return (unsigned int)BOOT_ST_AB_HSE;
    }

    g_abSwapDone = 1U;
    return (unsigned int)BOOT_ST_AB_OK;
}

/* Swap to the APP that is already present in the passive partition: verify it
 * against kBlock3_Pre_*, verify the passive BOOT image, shift the Block3
 * entries and ask the HSE to activate the passive block.
 *
 * Used by 0xFF02 when only a new BOOT was downloaded (the APP does not change,
 * the passive APP becomes the active one after the swap) and by
 * 0x31 01 AC C1.
 *
 * @return BOOT_ST_AB_xxx status */
static unsigned int BootProg_SwapToPre(void)
{
    unsigned char preSha[HSE_HASH_SHA256_SIZE];
    unsigned char digest[HSE_HASH_SHA256_SIZE];
    unsigned int preSize = 0U;

    if (0U != g_abSwapDone)
    {
        return (unsigned int)BOOT_ST_AB_PRECOND;
    }

    /* The passive APP is described by kBlock3_Pre_*.  A never programmed block
     * reads as 0xFF, so preSize becomes 0xFFFFFFFF and is rejected.
     *
     * New board (no APP programmed anywhere): there is no APP to protect, the
     * ECU stays in the bootloader after the swap.  The BOOT that is activated
     * is verified below either way. */
    if (0U == BootProg_NoAppProgrammed())
    {
        if ((0U != BootProg_Block3Read(kBlock3_Pre_SHAdata, kBlock3_Pre_FileSize,
                                       preSha, &preSize)) ||
            (0U == preSize) || (preSize > BOOT_APP_SIZE))
        {
            return (unsigned int)BOOT_ST_AB_NO_PRE;
        }

        /* Refuse to activate a passive partition whose APP does not match the
         * SHA-256 recorded for it (e.g. an interrupted download). */
        if (0U == Boot_PassiveAppValid())
        {
            return (unsigned int)BOOT_ST_AB_APP_HEADER;
        }

        if (0U != BootProg_HashRegion(BOOT_APP_BACKUP_BASE, preSize, digest))
        {
            return (unsigned int)BOOT_ST_AB_HSE_HASH;
        }

        if (0 != memcmp(digest, preSha, HSE_HASH_SHA256_SIZE))
        {
            return (unsigned int)BOOT_ST_AB_PRE_INVALID;
        }
    }

    /* Passive BOOT: a BOOT downloaded in this session is verified against the
     * 0x37 digest, otherwise against the SHA-256 stored for the passive image
     * (kBlock3_PreBootSHAdata).  Both paths fail closed. */
    if (0U != g_bootImage.Valid)
    {
        if (0U == Boot_ImageHeaderValid(BOOT_CODE_BACKUP_BASE, BOOT_CODE_BASE,
                                        BOOT_CODE_BACKUP_SIZE))
        {
            return (unsigned int)BOOT_ST_AB_NO_BOOT;
        }

        if (0U != BootProg_HashRegion(BOOT_CODE_BACKUP_BASE, g_bootImage.Size,
                                      digest))
        {
            return (unsigned int)BOOT_ST_AB_HSE_HASH;
        }

        if (0 != memcmp(digest, g_bootImage.Hash, HSE_HASH_SHA256_SIZE))
        {
            return (unsigned int)BOOT_ST_AB_BOOT_HASH;
        }
    }
    else if (0U == Boot_PassiveBootValid())
    {
        return (unsigned int)BOOT_ST_AB_NO_BOOT;
    }

    return BootProg_RequestSwap();
}

unsigned char BootProg_SwapStep(unsigned int *status, unsigned char *more)
{
    unsigned char digest[HSE_HASH_SHA256_SIZE];

    *status = (unsigned int)BOOT_ST_AB_OK;
    *more = 0U;
    g_bootAbSwapDiag[2] = 1U;

    if (0U == BSW_stUpdateVerValid)
    {
        *status = (unsigned int)BOOT_ST_AB_NOT_CHECKED;
        g_bootAbSwapDiag[0] = *status;
        return 0U;
    }

    if (0U != g_abSwapDone)
    {
        *status = (unsigned int)BOOT_ST_AB_PRECOND;
        g_bootAbSwapDiag[0] = *status;
        return 0U;
    }

    if (0U == g_appImage.Valid)
    {
        if (0U == g_bootImage.Valid)
        {
            *status = (unsigned int)BOOT_ST_AB_PRECOND;
            g_bootAbSwapDiag[0] = *status;
            return 0U;
        }

        /* BOOT-only update: the passive APP does not change, so the APP/BOOT
         * verification and the Block3 shift are the same as for ACC1 (the two
         * pairs are exchanged). */
        *status = BootProg_SwapToPre();
        if ((unsigned int)BOOT_ST_AB_OK == *status)
        {
            BootProg_CountProgramSuccess();
        }
        g_bootAbSwapDiag[0] = *status;
        return 0U;
    }

    /* Passive APP: header first, then the SHA-256 read back from flash. */
    if (0U == Boot_PassiveAppValid())
    {
        *status = (unsigned int)BOOT_ST_AB_APP_HEADER;
        g_bootAbSwapDiag[0] = *status;
        return 0U;
    }

    if (0U != BootProg_HashRegion(BOOT_APP_BACKUP_BASE, g_appImage.Size,
                                  digest))
    {
        *status = (unsigned int)BOOT_ST_AB_HSE_HASH;
        g_bootAbSwapDiag[0] = *status;
        return 0U;
    }

    if (0 != memcmp(digest, g_appImage.Hash, HSE_HASH_SHA256_SIZE))
    {
        *status = (unsigned int)BOOT_ST_AB_APP_HASH;
        g_bootAbSwapDiag[0] = *status;
        return 0U;
    }

    /* A BOOT image downloaded in the same session must be valid too: the
     * tester sent it at 0x00400000 and it was programmed at 0x00600000.  If no
     * new BOOT was downloaded, the image already present in the passive
     * partition has to match the SHA-256 stored for it (kBlock3_PreBoot*). */
    if (0U != g_bootImage.Valid)
    {
        if (0U == Boot_ImageHeaderValid(BOOT_CODE_BACKUP_BASE, BOOT_CODE_BASE,
                                        BOOT_CODE_BACKUP_SIZE))
        {
            *status = (unsigned int)BOOT_ST_AB_NO_BOOT;
            g_bootAbSwapDiag[0] = *status;
            return 0U;
        }

        if (0U != BootProg_HashRegion(BOOT_CODE_BACKUP_BASE, g_bootImage.Size,
                                      digest))
        {
            *status = (unsigned int)BOOT_ST_AB_HSE_HASH;
            g_bootAbSwapDiag[0] = *status;
            return 0U;
        }

        if (0 != memcmp(digest, g_bootImage.Hash, HSE_HASH_SHA256_SIZE))
        {
            *status = (unsigned int)BOOT_ST_AB_BOOT_HASH;
            g_bootAbSwapDiag[0] = *status;
            return 0U;
        }
    }
    else if (0U == Boot_PassiveBootValid())
    {
        /* A partition without a valid BOOT image cannot be started after the
         * swap, so refuse rather than brick the ECU. */
        *status = (unsigned int)BOOT_ST_AB_NO_BOOT;
        g_bootAbSwapDiag[0] = *status;
        return 0U;
    }

    /* Block3 (both pairs) + HSE ACTIVATE_PASSIVE_BLOCK, with a rollback of
     * block 3 when the HSE refuses the swap. */
    *status = BootProg_RequestSwap();
    if ((unsigned int)BOOT_ST_AB_OK == *status)
    {
        BootProg_CountProgramSuccess();
    }

    g_bootAbSwapDiag[0] = *status;
    return 0U;
}

unsigned char BootProg_AbSwap(unsigned int *status)
{
    g_bootAbSwapDiag[2] = 2U;

    /* Rollback with no new image: same "swap to the passive APP" operation as
     * the BOOT-only 0xFF02 path. */
    *status = BootProg_SwapToPre();
    g_bootAbSwapDiag[0] = *status;

    return 0U;
}

unsigned char BootProg_CheckIntegrity(const unsigned char *expected,
                                      unsigned int *status)
{
    *status = BOOT_ST_CHK_OK;

    if ((0U == g_prog.LastHashValid) || (0 == expected))
    {
        *status = BOOT_ST_CHK_NO_DOWNLOAD;
        return 0U;
    }

    /* 直接复用 0x37 时由 HSE 算出的 SHA-256（fs 和 APP 都算），
     * 与上位机从 0x37 响应里拿到的摘要是同一个。
     * 这是兼容旧上位机的 32 字节明文摘要路径。 */
    if (0 != memcmp(expected, g_prog.LastHash, HSE_HASH_SHA256_SIZE))
    {
        *status = BOOT_ST_CHK_HASH_MISMATCH;
    }

    return 0U;
}

/* 0x31 01 02 02 (RSA): the tester sends a 256-byte RSA-2048 block that
 * contains the SHA-256 of the downloaded image, encrypted with the matching
 * private key.  HseRsa_VerifyDigest() performs the public-key operation
 * (HSE SIGN/VERIFY or RSA decrypt) and compares the recovered digest with the
 * value computed by 0x37. */
unsigned char BootProg_CheckIntegritySignature(const unsigned char *signature,
                                               unsigned int signatureLen,
                                               unsigned int *status)
{
    uint8_t rc;

    *status = BOOT_ST_CHK_OK;

    if ((0U == g_prog.LastHashValid) || (0 == signature))
    {
        *status = BOOT_ST_CHK_NO_DOWNLOAD;
        return 0U;
    }

    if ((unsigned int)HSE_RSA_SIGNATURE_SIZE != signatureLen)
    {
        *status = BOOT_ST_CHK_RSA;
        return 0U;
    }

    rc = HseRsa_VerifyDigest(signature, (uint16_t)signatureLen, g_prog.LastHash);

    if (HSE_RSA_OK == rc)
    {
        *status = BOOT_ST_CHK_OK;
    }
    else if (HSE_RSA_ERR_HASH_MISMATCH == rc)
    {
        *status = BOOT_ST_CHK_HASH_MISMATCH;
    }
    else
    {
        *status = BOOT_ST_CHK_RSA;
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






