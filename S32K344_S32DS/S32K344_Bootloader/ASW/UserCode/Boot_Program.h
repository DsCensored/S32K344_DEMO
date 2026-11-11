/*
 * Boot_Program.h
 *
 * UDS programming services of the bootloader:
 *   - 0x34/0x36/0x37 download of the flash driver (ITCM) and of the APP
 *     (backup area),
 *   - dependency check (APP + flash driver),
 *   - erase of a given address range, one sector per DCM poll,
 *   - 8 KB block copy from the backup area to the run area.
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
#define BOOT_PROG_COPY_CHUNK    0x2000U   /* 8 KB per DCM poll */

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
#define BOOT_ST_DEP_APP_HEADER      0x11U   /* 备份区 APP 头无效 */
#define BOOT_ST_DEP_APP_INCOMPLETE  0x12U   /* 没有完整的 APP 下载 */
#define BOOT_ST_DEP_APP_HASH        0x13U   /* APP SHA-256 不一致 */
#define BOOT_ST_DEP_HSE             0x14U   /* HSE SHA-256 计算失败 */

/* 0xFF02：备份区 -> 运行区拷贝 */
#define BOOT_ST_COPY_OK             0x00U
#define BOOT_ST_COPY_NOT_CHECKED    0x01U   /* 未通过 0xFF01 依赖校验 */
#define BOOT_ST_COPY_PRECOND        0x02U   /* 前置条件不满足 */
#define BOOT_ST_COPY_ERASE          0x03U   /* 运行区擦除失败 */
#define BOOT_ST_COPY_WRITE          0x04U   /* 写入失败 */
#define BOOT_ST_COPY_HASH           0x05U   /* 拷贝后 SHA-256 不一致 */
#define BOOT_ST_COPY_HSE            0x06U   /* HSE SHA-256 计算失败 */

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
 * version policy is skipped when the last download was the flash driver (fs). */
unsigned char BootProg_DependencyCheck(unsigned int *status);
/* Copy 8 KB per call; *more is set when the DCM must call again.  The result
 * is reported in *status (BOOT_ST_COPY_xxx); the function always returns 0. */
unsigned char BootProg_CopyStep(unsigned int *status, unsigned char *more);

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
