/*
 * Boot_Program.h
 *
 * UDS programming services of the bootloader:
 *   - 0x34/0x36/0x37 download of the flash driver (ITCM), of the BOOT image
 *     and of the APP image (both into the passive flash partition),
 *   - dependency check (BOOT + APP + flash driver),
 *   - erase of a given address range, one sector per DCM poll,
 *   - 0xFF02 finalisation of an AB swap: verify the passive images, shift the
 *     SHA-256 values in NVM block 3 and ask the HSE to activate the passive
 *     block,
 *   - 0x31 01 AC C1 AB swap without a new download (rollback / BOOT-only
 *     update): exchange the two SHA-256 values and activate the passive block.
 *
 * The tester always addresses an image by its run address:
 *     BOOT 0x00400000  -> programmed at 0x00600000 (passive)
 *     APP  0x00448000  -> programmed at 0x00648000 (passive)
 * The HSE swaps the active/passive partition at the next reset, so the
 * images appear at the run addresses again.
 *
 * Image integrity is checked with the HSE SHA-256 engine (hse_hash.h): the
 * digest is computed over the image as it is stored (ITCM or PFlash), not over
 * the bytes received on CAN, so a flash programming error is detected too.
 *
 * 0x37 response:   77 <SHA-256:32>
 * 0x31 01 02 02 :  31 01 02 02 <RSA-2048 signature:256> -> RSA public-key
 *                  operation with the AA01 key, recovered SHA-256 compared
 *                  with the last 0x37 digest.
 *
 * Set BOOT_RSA_ALLOW_PLAIN_HASH to 0 in production: 0202 then only accepts
 * the 256-byte RSA block.  The default 1 keeps the previous 32-byte
 * plaintext digest working while the tester is being updated.
 */

#ifndef BOOT_PROGRAM_H_
#define BOOT_PROGRAM_H_

#include "hse_hash.h"      /* HSE_HASH_SHA256_SIZE */
#include "hse_rsa.h"       /* HSE_RSA_SIGNATURE_SIZE */

#ifndef BOOT_RSA_ALLOW_PLAIN_HASH
#define BOOT_RSA_ALLOW_PLAIN_HASH   1
#endif

#define BOOT_PROG_MAX_BLOCK     500U      /* data bytes per 0x36 request */

/* ---------------- 0x31 例程结果码 ----------------
 * 例程控制统一回正响应 71 <subFn> <RID> <status>：
 *   status = 0 表示成功，非 0 表示失败原因（不再回 NRC 0x22）。
 * 报文格式/子功能/例程号/安全等级错误仍由 DCM 层回相应 NRC。
 */

/* 0x0202：文件校验（与最后一次 0x37 下载的镜像 SHA-256 比较） */
#define BOOT_ST_CHK_OK              0x00U
#define BOOT_ST_CHK_HASH_MISMATCH   0x01U   /* SHA-256 与下载内容不一致 */
#define BOOT_ST_CHK_NO_DOWNLOAD     0x02U   /* 没有有效的下载结果 */
#define BOOT_ST_CHK_RSA             0x03U   /* RSA 解密/验签失败（密钥/HSE） */

/* 0xFF00：擦除 */
#define BOOT_ST_ERASE_OK            0x00U
#define BOOT_ST_ERASE_RANGE         0x01U   /* 地址/长度非法 */
#define BOOT_ST_ERASE_DRIVER        0x02U   /* FlashDriver 无效/初始化失败 */
#define BOOT_ST_ERASE_FAIL          0x03U   /* 擦除失败 */

/* 0xFF01：依赖校验 */
#define BOOT_ST_DEP_OK              0x00U
#define BOOT_ST_DEP_ECU             0x01U   /* ECU 型号不一致 */
#define BOOT_ST_DEP_HW              0x02U   /* 硬件版本不一致 */
#define BOOT_ST_DEP_VER_NOW         0x04U   /* 当前 APP 版本解析失败 */
#define BOOT_ST_DEP_VER_NEW         0x05U   /* 新 APP 版本解析失败 */
#define BOOT_ST_DEP_VER_DOWN        0x06U   /* 非正向升级（降级/同级） */
#define BOOT_ST_DEP_DRIVER          0x10U   /* FlashDriver 无效 */
#define BOOT_ST_DEP_APP_HEADER      0x11U   /* 被动区 APP 头无效 */
#define BOOT_ST_DEP_APP_INCOMPLETE  0x12U   /* 没有完整的 APP/BOOT 下载 */
#define BOOT_ST_DEP_APP_HASH        0x13U   /* APP SHA-256 不一致 */
#define BOOT_ST_DEP_HSE             0x14U   /* HSE SHA-256 计算失败 */
#define BOOT_ST_DEP_BOOT_HEADER     0x15U   /* 被动区 BOOT 头无效 */
#define BOOT_ST_DEP_BOOT_HASH       0x16U   /* BOOT SHA-256 不一致 */
#define BOOT_ST_DEP_PRE_INVALID     0x17U   /* 只刷 BOOT：被动区 APP 与 Block3 Pre 不一致 */

/* 0xFF02 / 0x31 01 AC C1：AB 切区（不再搬运，改为 HSE 分区切换） */
#define BOOT_ST_AB_OK               0x00U
#define BOOT_ST_AB_NOT_CHECKED      0x01U   /* 未通过 0xFF01 依赖校验 */
#define BOOT_ST_AB_PRECOND          0x02U   /* 下载信息不满足 / 已请求过切区 */
#define BOOT_ST_AB_APP_HEADER       0x03U   /* 被动区 APP 头/向量表非法 */
#define BOOT_ST_AB_APP_HASH         0x04U   /* 被动区 APP SHA-256 不一致 */
#define BOOT_ST_AB_NO_BOOT          0x05U   /* 被动区 BOOT 镜像无效 */
#define BOOT_ST_AB_BOOT_HASH        0x06U   /* 新 BOOT SHA-256 不一致 */
#define BOOT_ST_AB_NVM              0x07U   /* Block3 SHA-256/长度写入失败 */
#define BOOT_ST_AB_HSE              0x08U   /* HSE ACTIVATE_PASSIVE_BLOCK 失败 */
#define BOOT_ST_AB_HSE_HASH         0x09U   /* HSE SHA-256 计算失败 */
#define BOOT_ST_AB_NO_PRE           0x0AU   /* ACC1：Block3 Pre SHA-256 无效 */
#define BOOT_ST_AB_PRE_INVALID      0x0BU   /* ACC1：被动区 APP 与 Pre 不一致 */

unsigned char BootProg_RequestDownload(unsigned int addr, unsigned int size,
                                       unsigned int *maxBlock);
unsigned char BootProg_TransferData(unsigned char seq, const unsigned char *data,
                                    unsigned int len);
/* Finish the download and return the SHA-256 of the stored image (32 bytes).
 * hash must point to at least HSE_HASH_SHA256_SIZE bytes. */
unsigned char BootProg_TransferExit(unsigned char *hash);

/* Erase one 8 KB sector per call; *more is set when the DCM must call again
 * (the handler then returns NRC 0x78).  The result is reported in *status
 * (BOOT_ST_ERASE_xxx); the function itself always returns 0. */
unsigned char BootProg_EraseStep(unsigned int addr, unsigned int len,
                                 unsigned int *status, unsigned char *more);
/* 0xFF01 dependency check.  The result is reported in *status
 * (BOOT_ST_DEP_xxx); the function always returns 0.  The forward-only APP
 * version policy is skipped when the last download was the flash driver (fs).
 *
 * Three cases are supported:
 *   - APP (and optional BOOT) downloaded: normal APP checks + optional BOOT;
 *   - only BOOT downloaded: the passive APP is checked against the SHA stored
 *     for it in Block3 (kBlock3_Pre_*); the version policy is skipped because
 *     the APP does not change;
 *   - only the flash driver: no image check.
 * A BOOT image downloaded in the same session is always verified too. */
unsigned char BootProg_DependencyCheck(unsigned int *status);
/* 0xFF02: finish an APP or BOOT download and request the HSE AB swap.
 * Nothing is copied: the tester has programmed the passive partition, and
 * after the next reset the HSE makes it the active one.
 *
 * APP case: the passive APP is hashed again and compared with the 0x37
 * digest, the passive BOOT image is validated and the SHA-256 / length of the
 * new APP is shifted into NVM block 3:
 *      kBlock3_Pre_SHAdata  <- old kBlock3_Now_SHAdata
 *      kBlock3_Now_SHAdata  <- SHA-256 of the image just downloaded
 * (same for the file lengths).
 *
 * BOOT-only case: the APP in the passive partition does not change, so the
 * two Block3 entries are exchanged (Pre <- Now, Now <- old Pre) - the passive
 * APP becomes the active one after the swap.  This is the same operation as
 * 0x31 01 AC C1; the tester updates the other partition with a second pass.
 *
 * The result is reported in *status (BOOT_ST_AB_xxx); *more is always 0.  It
 * is kept in the signature so the DCM handler does not have to change if a
 * future step needs several polls. */
unsigned char BootProg_SwapStep(unsigned int *status, unsigned char *more);

/* 0x31 01 AC C1: AB swap without a new APP or BOOT download (rollback).
 * The passive APP must still match kBlock3_Pre_SHAdata, the passive BOOT
 * image must be valid, then the two block 3 entries are exchanged
 * (Pre <- Now, Now <- old Pre) and the HSE is asked to activate the passive
 * block.  The result is reported in *status (BOOT_ST_AB_xxx); the function
 * itself always returns 0. */
unsigned char BootProg_AbSwap(unsigned int *status);

/* QCC 04.171 31 01 02 02: compare the tester SHA-256 with the digest of the
 * last image, which is computed by the HSE while finishing 0x37 (the same
 * value that 0x37 returned).  expected points to HSE_HASH_SHA256_SIZE bytes.
 * The result is reported in *status (BOOT_ST_CHK_xxx); the function always
 * returns 0 (0 = digest valid). */
unsigned char BootProg_CheckIntegrity(const unsigned char *expected,
                                      unsigned int *status);

/* QCC 04.171 31 01 02 02 with RSA: signature points to a 256-byte RSA-2048
 * block.  The public key installed by AA01 is used to recover the SHA-256,
 * which is compared with the digest of the last image.  The result is
 * reported in *status (BOOT_ST_CHK_xxx); the function always returns 0. */
unsigned char BootProg_CheckIntegritySignature(const unsigned char *signature,
                                               unsigned int signatureLen,
                                               unsigned int *status);

/* QCC 04.171 2E F1 84: fingerprint interface.  Fee is not integrated yet,
 * so the current implementation keeps the 9 bytes in RAM. */
unsigned char BootProg_WriteFingerprint(const unsigned char *data);
unsigned char BootProg_ReadFingerprint(unsigned char *data);

/* Destroy the downloaded ITCM driver (DEINIT + ITCM wipe).  Safe to call
 * repeatedly; the next programming event must download the driver again. */
void BootProg_DestroyFlash(void);

#endif /* BOOT_PROGRAM_H_ */
