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
#include "hse_hash.h"     /* HSE SHA-256 used for the image integrity checks */

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
    unsigned char Seq;
    unsigned char Buf[BOOT_PROG_BUF_SIZE] __attribute__((aligned(4)));
    unsigned int BufLen;
    unsigned char AppHashValid;
    unsigned char AppHash[HSE_HASH_SHA256_SIZE];   /* SHA-256 of the APP image */
    unsigned int AppSize;
    unsigned char LastHashValid;           /* result of the last 0x37 (APP or fs) */
    unsigned char LastHash[HSE_HASH_SHA256_SIZE];
} Boot_ProgType;

static Boot_ProgType g_prog;

static unsigned char g_flashReady;
static unsigned char g_fingerprint[9];

/* Set when the last 0x34/0x36/0x37 job downloaded the flash driver (fs) into
 * ITCM.  The APP version policy only makes sense for the APP image, so the
 * dependency check must not run check_version() while fs was the last image. */
static unsigned char g_progImageIsDriver;

/* Set by 0xFF01 only when every APP dependency check passed; it authorises
 * the 0xFF02 copy.  Defined below. */
extern unsigned char BSW_stUpdateVerValid;

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
static unsigned char g_copyBuf[BOOT_PROG_COPY_CHUNK + BOOT_FLASH_WRITE_UNIT]
    __attribute__((aligned(4)));

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

    /* A new download invalidates any erase/copy state of a previous one, and
     * the dependency check of the previous image no longer applies. */
    g_copyState = 0U;
    BSW_stUpdateVerValid = 0U;

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
     * backup area for the APP.  The 0xFF padding of the last 128-byte write
     * page is outside g_prog.Size and is therefore not hashed. */
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
    else
    {
        /* A new APP image invalidates a previous copy progress. */
        g_copyState = 0U;
    }

    if (0U == nrc)
    {
        if (BOOT_PROG_MODE_RAM != g_prog.Mode)
        {
            (void)memcpy(g_prog.AppHash, digest, sizeof(g_prog.AppHash));
            g_prog.AppSize = g_prog.Size;
            g_prog.AppHashValid = 1U;
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

    *status = BOOT_ST_ERASE_OK;
    *more = 0U;

    if ((0U == len) || (addr > 0x007FFFFFUL) || (len > 0x00800000UL))
    {
        *status = BOOT_ST_ERASE_RANGE;
        return 0U;
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

unsigned char BSW_stUpdateVerValid = 0;
unsigned char BootProg_DependencyCheck(unsigned int *status)
{
    *status = BOOT_ST_DEP_OK;

    /* 只有本次校验全部通过才允许 0xFF02 拷贝。 */
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

    if (check_version(status) != 0U)
    {
        return 0U;
    }

    /* The downloaded image lives in the backup area: check its header. */
    if (BOOT_APP_HEADER_MAGIC != *(volatile unsigned int *)BOOT_APP_BACKUP_BASE)
    {
        *status = BOOT_ST_DEP_APP_HEADER;
        return 0U;
    }

    if (0U == g_prog.AppHashValid)
    {
        *status = BOOT_ST_DEP_APP_INCOMPLETE;
        return 0U;
    }

    /* Re-read the backup area and hash it again: this also catches a flash
     * programming error that the CAN reception obviously cannot see. */
    {
        unsigned char digest[HSE_HASH_SHA256_SIZE];

        if (0U != BootProg_HashRegion(BOOT_APP_BACKUP_BASE, g_prog.AppSize,
                                      digest))
        {
            *status = BOOT_ST_DEP_HSE;
            return 0U;
        }

        if (0 != memcmp(digest, g_prog.AppHash, HSE_HASH_SHA256_SIZE))
        {
            *status = BOOT_ST_DEP_APP_HASH;
            return 0U;
        }
    }

    BSW_stUpdateVerValid = 1U;
    return 0U;
}

unsigned char BootProg_CopyStep(unsigned int *status, unsigned char *more)
{
    unsigned int chunk;
    unsigned int remaining;

    *status = BOOT_ST_COPY_OK;
    *more = 0U;

    if ((0U == g_prog.AppHashValid) || (0U == Boot_FlashDriverValid()) ||
        (0U != BootProg_InitFlash()))
    {
        g_copyState = 0U;
        *status = BOOT_ST_COPY_PRECOND;
        return 0U;
    }

    if (0U == g_copyState)
    {
        g_copyOffset = 0U;

        if (0U != Boot_FlashErase(BOOT_APP_BASE,
                                  (g_prog.AppSize + BOOT_APP_ERASE_UNIT - 1U) &
                                  ~(BOOT_APP_ERASE_UNIT - 1U)))
        {
            *status = BOOT_ST_COPY_ERASE;
            return 0U;
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
        *status = BOOT_ST_COPY_WRITE;
        g_copyState = 0U;
        return 0U;
    }

    g_copyOffset += chunk;

    if (g_copyOffset < g_prog.AppSize)
    {
        *more = 1U;
        return 0U;
    }

    g_copyState = 0U;

    /* The copy is complete: hash the run area as it has been written and
     * compare it with the digest of the downloaded image. */
    {
        unsigned char digest[HSE_HASH_SHA256_SIZE];

        if (0U != BootProg_HashRegion(BOOT_APP_BASE, g_prog.AppSize, digest))
        {
            *status = BOOT_ST_COPY_HSE;
            return 0U;
        }

        if (0 != memcmp(digest, g_prog.AppHash, HSE_HASH_SHA256_SIZE))
        {
            *status = BOOT_ST_COPY_HASH;
            return 0U;
        }
    }

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






