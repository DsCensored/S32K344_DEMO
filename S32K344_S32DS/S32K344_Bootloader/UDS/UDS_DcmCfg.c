/*
 * UDS_DcmCfg.c
 *
 *  DCM user configuration:
 *    - service table with session, security and addressing requirements
 *      (0x10/0x11/0x22/0x27/0x2E/0x31)
 *    - service handlers
 *    - security seed/key algorithm
 *    - routine table and routine handlers
 *    - application-specific DCM timers
 */

#include "UDS_DcmCfg.h"
#include "UDS_CanTp.h"
#include "UserOS.h"
#include "Mcu.h"
#include "Boot.h"
#include "Boot_Shared.h"
#include "Boot_Program.h"
#include "App_Info.h"
#include "Nvm.h"
#include "NvmApp.h"
#include <string.h>
#include "hse_trng.h"     /* HSE 硬件 TRNG，供 UDS 0x27 取随机种子 */
#include "hse_aes.h"      /* HSE AES-128，供 UDS 0x27 key 计算 */
#include "hse_hash.h"     /* HSE SHA-256，供刷写镜像完整性校验 */

UDS_DcmContextType UDS_DcmCtx;

/* UDS 0x27：16 字节种子 + AES128 预期密钥（文件内静态，避免改上下文结构体） */
static unsigned char UDS_Seed16[16];
static unsigned char UDS_ExpectedKey[16];

static unsigned char UDS_DcmPreconditionOk = 0U;

static unsigned char UDS_DcmRequestReset(unsigned char subFn, unsigned int *rspLen);

/* ------------------------------------------------------------------------- */
/* Session / reset                                                           */
/* ------------------------------------------------------------------------- */

static unsigned char UDS_DcmSetSession(UDS_DcmSessionType session, unsigned int *rspLen)
{
	UDS_DcmCtx.Session = session;
	UDS_DcmCtx.SessionTick = BSW_GetSystemTick();

	/* A session change locks security again. */
	UDS_DcmCtx.SecurityUnlocked = 0U;
	UDS_DcmCtx.Seed = 0U;
	UDS_DcmCtx.SeedLevel = 0U;
	UDS_DcmCtx.SecurityFailCount = 0U;

	UDS_TxBuf[1] = (unsigned char)session;
	*rspLen = 2U;
	return 0U;
}

static unsigned char UDS_DcmSessionDefault(const unsigned char *req, unsigned int len, unsigned int *rspLen)
{
	(void)req;
	(void)len;

	/* QCC 04.171: in the programming session 10 01 resets the ECU. */
	if (UDS_DCM_SESSION_PROGRAMMING == UDS_DcmCtx.Session)
	{
		return UDS_DcmRequestReset(0x01U, rspLen);
	}

	return UDS_DcmSetSession(UDS_DCM_SESSION_DEFAULT, rspLen);
}

static unsigned char UDS_DcmSessionProgramming(const unsigned char *req, unsigned int len, unsigned int *rspLen)
{
	(void)req;
	(void)len;

	/* Drop any driver image from a previous session before a new one. */
	BootProg_DestroyFlash();

	/* QCC 04.171: 10 02 is only accepted after 31 01 02 03. */
	if (0U == UDS_DcmPreconditionOk)
	{
		return (unsigned char)NRC_CONDITIONS_NOT_CORRECT;
	}

	Boot_StayInBoot();
	UDS_DcmPreconditionOk = 0U;

	return UDS_DcmSetSession(UDS_DCM_SESSION_PROGRAMMING, rspLen);
}

static unsigned char UDS_DcmSessionExtended(const unsigned char *req, unsigned int len, unsigned int *rspLen)
{
	(void)req;
	(void)len;
	return UDS_DcmSetSession(UDS_DCM_SESSION_EXTENDED, rspLen);
}

static unsigned char UDS_DcmRequestReset(unsigned char subFn, unsigned int *rspLen)
{
	UDS_TxBuf[1] = subFn;
	*rspLen = 2U;

	UDS_DcmCtx.ResetPending = 1U;
	UDS_DcmCtx.ResetTick = BSW_GetSystemTick();
	return 0U;
}

static unsigned char UDS_DcmKeepSession(const unsigned char *req, unsigned int len, unsigned int *rspLen)
{
	(void)req;
	(void)len;
	switch(UDS_DcmCtx.Session)
	{
	case UDS_DCM_SESSION_DEFAULT:
		UDS_DcmSetSession(UDS_DCM_SESSION_DEFAULT, rspLen);
		break;
	case UDS_DCM_SESSION_PROGRAMMING:
		UDS_DcmSetSession(UDS_DCM_SESSION_PROGRAMMING, rspLen);
		break;
	case UDS_DCM_SESSION_EXTENDED:
		UDS_DcmSetSession(UDS_DCM_SESSION_EXTENDED, rspLen);
		break;
	}

	UDS_TxBuf[1] = 0x00;
	*rspLen = 2U;

	return 0;
}


static unsigned char UDS_DcmResetSecurity(void)
{
	unsigned char required = (UDS_DCM_SESSION_PROGRAMMING == UDS_DcmCtx.Session)
	    ? UDS_DCM_SECURITY_LEVEL2 : UDS_DCM_SECURITY_LEVEL1;

	if (0U == (UDS_DcmCtx.SecurityUnlocked & required))
	{
		return (unsigned char)NRC_SECURITY_ACCESS_DENIED;
	}

	return 0U;
}

static unsigned char UDS_DcmResetHard(const unsigned char *req, unsigned int len, unsigned int *rspLen)
{
	unsigned char nrc = UDS_DcmResetSecurity();
	(void)req;
	(void)len;
	if (0U != nrc)
	{
		return nrc;
	}
	return UDS_DcmRequestReset(0x01U, rspLen);
}

static unsigned char UDS_DcmResetKeyOffOn(const unsigned char *req, unsigned int len, unsigned int *rspLen)
{
	unsigned char nrc = UDS_DcmResetSecurity();
	(void)req;
	(void)len;
	if (0U != nrc)
	{
		return nrc;
	}
	return UDS_DcmRequestReset(0x02U, rspLen);
}

static unsigned char UDS_DcmResetSoft(const unsigned char *req, unsigned int len, unsigned int *rspLen)
{
	unsigned char nrc = UDS_DcmResetSecurity();
	(void)req;
	(void)len;
	if (0U != nrc)
	{
		return nrc;
	}
	return UDS_DcmRequestReset(0x03U, rspLen);
}

/* ------------------------------------------------------------------------- */
/* Security access 0x27                                                      */
/* ------------------------------------------------------------------------- */

static unsigned char UDS_DcmSeedRequest(const unsigned char *req, unsigned int len, unsigned int *rspLen)
{
	unsigned char subFn;
	unsigned char level;

	if (len < 2U)
	{
		return NRC_INCORRECT_MESSAGE_LENGTH_OR_INVALID_FORMAT;
	}

	subFn = (unsigned char)(req[1] & 0x7FU);
	level = (0x01U == subFn) ? 1U : 2U;

	if ((0x01U != subFn) && (0x03U != subFn))
	{
		return NRC_SUBFUNCTION_NOT_SUPPORTED;
	}

	if (0U != UDS_DcmCtx.SecurityDelayActive)
	{
		return NRC_REQUIRED_TIME_DELAY_NOT_EXPIRED;
	}

	UDS_DcmCtx.SeedLevel = level;

	/* 16 字节种子（HSE TRNG）；密钥 = AES128(HSE 密钥[level], seed)。
	 * TRNG 不可用时用 tick 派生的伪随机填充，保证 0x27 不失效。 */
	if (0U != HseTrng_GetBytes(UDS_Seed16, 16U))
	{
		unsigned int t = BSW_GetSystemTick();
		unsigned char k;
		for (k = 0U; k < 16U; k++)
		{
			t = (t * 1664525U) + 1013904223U;
			UDS_Seed16[k] = (unsigned char)(t >> 24);
		}
	}
	UDS_DcmCtx.Seed = ((unsigned int)UDS_Seed16[0] << 24) | ((unsigned int)UDS_Seed16[1] << 16)
	                | ((unsigned int)UDS_Seed16[2] << 8)  |  (unsigned int)UDS_Seed16[3];

	UDS_TxBuf[1] = subFn;
	(void)memcpy(&UDS_TxBuf[2], UDS_Seed16, 16U);	
	*rspLen = 18U;

	return 0U;
}

static unsigned char UDS_DcmKeyRequest(const unsigned char *req, unsigned int len, unsigned int *rspLen)
{
	unsigned char subFn;
	unsigned char level;
	unsigned int key;

	if (len < 18U)
	{
		return NRC_INCORRECT_MESSAGE_LENGTH_OR_INVALID_FORMAT;
	}

	subFn = (unsigned char)(req[1] & 0x7FU);
	level = (0x02U == subFn) ? 1U : 2U;

	if ((0x02U != subFn) && (0x04U != subFn))
	{
		return NRC_SUBFUNCTION_NOT_SUPPORTED;
	}

	if (0U != UDS_DcmCtx.SecurityDelayActive)
	{
		return NRC_REQUIRED_TIME_DELAY_NOT_EXPIRED;
	}

	if (UDS_DcmCtx.SeedLevel != level)
	{
		return NRC_REQUEST_SEQUENCE_ERROR;
	}

	key = ((unsigned int)req[2] << 24) |
	      ((unsigned int)req[3] << 16) |
	      ((unsigned int)req[4] << 8) |
	      (unsigned int)req[5];

	if (0U != HseAes_EncryptBlock((1U == level) ? HSE_AES_LEVEL1 : HSE_AES_LEVEL2, UDS_Seed16, UDS_ExpectedKey))
	{
		return NRC_CONDITIONS_NOT_CORRECT;
	}
	(void)key;
	if (0 != memcmp(&req[2], UDS_ExpectedKey, 16U))
	{
		UDS_DcmCtx.SecurityFailCount++;

		if (UDS_DcmCtx.SecurityFailCount >= UDS_DCM_SECURITY_MAX_ATTEMPTS)
		{
			UDS_DcmCtx.SecurityFailCount = 0U;
			UDS_DcmCtx.SecurityDelayActive = 1U;
			UDS_DcmCtx.SecurityDelayTick = BSW_GetSystemTick();
			return NRC_EXCEEDED_NUMBER_OF_ATTEMPTS;
		}

		return NRC_INVALID_KEY;
	}

	UDS_DcmCtx.SecurityUnlocked |= level;
	UDS_DcmCtx.SecurityFailCount = 0U;
	UDS_DcmCtx.Seed = 0U;
	UDS_DcmCtx.SeedLevel = 0U;

	UDS_TxBuf[1] = subFn;
	*rspLen = 2U;
	return 0U;
}

/* ------------------------------------------------------------------------- */
/* Data by identifier 0x22 / 0x2E                                            */
/* ------------------------------------------------------------------------- */

/*
 * 0xF190 (VIN-like data, 17 bytes).
 *
 * Stored in NVM block 0 with magic + checksum (see NvmApp.h), i.e. the same
 * DFlash block the application uses: 22 F190 returns the VIN written by the
 * application (or by the bootloader), 2E F190 stores it once.
 */

static unsigned char UDS_DcmF184Read(const UDS_DcmDidType *did,
                                     unsigned char *buf,
                                     unsigned int maxLen,
                                     unsigned int *outLen)
{
	(void)did;

	if (maxLen < 9U)
	{
		return NRC_RESPONSE_TOO_LONG;
	}

	(void)BootProg_ReadFingerprint(buf);
	*outLen = 9U;
	return 0U;
}

static unsigned char UDS_DcmF184Write(const UDS_DcmDidType *did,
                                      const unsigned char *buf,
                                      unsigned int len)
{
	(void)did;

	if (9U != len)
	{
		return NRC_INCORRECT_MESSAGE_LENGTH_OR_INVALID_FORMAT;
	}

	if (0U != BootProg_WriteFingerprint(buf))
	{
		return NRC_GENERAL_PROGRAMMING_FAILURE;
	}

	return 0U;
}

uint8 BSW_stForceUpdate = 0;
static unsigned char UDS_DcmF7F7Write(const UDS_DcmDidType *did,
                                      const unsigned char *buf,
                                      unsigned int len)
{
	(void)did;

	if (1 != len)
	{
		return NRC_INCORRECT_MESSAGE_LENGTH_OR_INVALID_FORMAT;
	}

	BSW_stForceUpdate = 1;

	return 0U;
}

static unsigned char UDS_DcmF190Read(const UDS_DcmDidType *did,
                                     unsigned char *buf,
                                     unsigned int maxLen,
                                     unsigned int *outLen)
{
	(void)did;

	if (maxLen < NVM_F190_VIN_LEN)
	{
		return NRC_RESPONSE_TOO_LONG;
	}

	/* No block validity check: an unprogrammed F190 simply reads as 0xFF. */
	if (E_OK != Nvm_F190_Read(buf))
	{
		return NRC_CONDITIONS_NOT_CORRECT;
	}

	*outLen = NVM_F190_VIN_LEN;
	return 0U;
}

static unsigned char UDS_DcmF190Write(const UDS_DcmDidType *did,
                                      const unsigned char *buf,
                                      unsigned int len)
{
	(void)did;

	if (len != NVM_F190_VIN_LEN)
	{
		return NRC_INCORRECT_MESSAGE_LENGTH_OR_INVALID_FORMAT;
	}

	/* Write once and commit: refused when a valid VIN already exists. */
	if (E_OK != Nvm_F190_Write(buf))
	{
		return NRC_GENERAL_PROGRAMMING_FAILURE;
	}

	return 0U;
}

/*
 * Version / identification DIDs.
 *
 * Every DID returns one 16-byte App_Info block read from a fixed flash
 * address.  The addresses are the same in the APP and in the BOOT image, so
 * the bootloader answers exactly like the application:
 *
 *   F180  boot software version    0x00400110 (boot image, .app_boot_ver_flash)
 *   F187  hardware version         0x00448120 (app  App_Info block 2)
 *   F189  application version      0x00448100 (app  App_Info block 0)
 *   F18A  board hardware version   0x00448110 (app  App_Info block 1)
 *   F18C  ECU serial number        0x00448300 (fixed serial area)
 *   F191  hardware part number     0x00448130 (app  App_Info block 3)
 *   F192  production / build date  0x00448160 (app  App_Info block 6)
 *   F19E  supplier code            0x00448140 (app  App_Info block 4)
 */
typedef struct
{
	unsigned short Did;
	unsigned long  Address;
} UDS_DcmFixedDidType;

static const UDS_DcmFixedDidType UDS_DcmFixedDids[] =
{
	{0xF180U, 0x00400110UL},
	{0xF187U, 0x00448120UL},
	{0xF189U, 0x00448100UL},
	{0xF18AU, 0x00448110UL},
	{0xF18CU, 0x00448300UL},
	{0xF191U, 0x00448130UL},
	{0xF192U, 0x00448160UL},
	{0xF19EU, 0x00448140UL}
};

static unsigned char UDS_DcmFixedDidRead(const UDS_DcmDidType *did,
                                         unsigned char *buf,
                                         unsigned int maxLen,
                                         unsigned int *outLen)
{
	unsigned int i;

	for (i = 0U; i < (unsigned int)(sizeof(UDS_DcmFixedDids) / sizeof(UDS_DcmFixedDids[0])); i++)
	{
		if (UDS_DcmFixedDids[i].Did == did->Did)
		{
			if (maxLen < APP_INFO_BLOCK_SIZE)
			{
				return NRC_RESPONSE_TOO_LONG;
			}

			memcpy(buf, (const void *)UDS_DcmFixedDids[i].Address,
			       APP_INFO_BLOCK_SIZE);
			*outLen = APP_INFO_BLOCK_SIZE;
			return 0U;
		}
	}

	return NRC_REQUEST_OUT_OF_RANGE;
}

const UDS_DcmDidType UDS_DcmDids[] =
{
	{0xF190U, UDS_DcmF190Read, UDS_DcmF190Write},
	{0xF184U, UDS_DcmF184Read, UDS_DcmF184Write},
	{0xF7F7U, 0, UDS_DcmF7F7Write},
	/* Version / identification DIDs (fixed flash addresses). */
	{0xF180U, UDS_DcmFixedDidRead, 0},
	{0xF187U, UDS_DcmFixedDidRead, 0},
	{0xF189U, UDS_DcmFixedDidRead, 0},
	{0xF18AU, UDS_DcmFixedDidRead, 0},
	{0xF18CU, UDS_DcmFixedDidRead, 0},
	{0xF191U, UDS_DcmFixedDidRead, 0},
	{0xF192U, UDS_DcmFixedDidRead, 0},
	{0xF19EU, UDS_DcmFixedDidRead, 0}
};

const unsigned int UDS_DcmDidCount =
	(unsigned int)(sizeof(UDS_DcmDids) / sizeof(UDS_DcmDids[0]));

static const UDS_DcmDidType *UDS_DcmFindDid(unsigned short did)
{
	unsigned int i;

	for (i = 0U; i < UDS_DcmDidCount; i++)
	{
		if (UDS_DcmDids[i].Did == did)
		{
			return &UDS_DcmDids[i];
		}
	}

	return 0;
}

static unsigned char UDS_DcmReadDataByIdentifier(const unsigned char *req,
                                                 unsigned int len,
                                                 unsigned int *rspLen)
{
	unsigned int in = 1U;
	unsigned int out = 1U;
	unsigned int dataLen;
	unsigned short did;
	unsigned char first = 1U;
	const UDS_DcmDidType *didCfg;
	unsigned char nrc;

	if ((len < 3U) || (0U != ((len - 1U) & 1U)))
	{
		return NRC_INCORRECT_MESSAGE_LENGTH_OR_INVALID_FORMAT;
	}

	while ((in + 1U) < len)
	{
		did = (unsigned short)(((unsigned short)req[in] << 8) | req[in + 1U]);
		didCfg = UDS_DcmFindDid(did);

		if (0 == didCfg)
		{
			return NRC_REQUEST_OUT_OF_RANGE;
		}

		if (0U != first)
		{
			UDS_TxBuf[1] = req[in];
			UDS_TxBuf[2] = req[in + 1U];
			out = 3U;
			first = 0U;
		}
		else
		{
			if ((out + 2U) > UDS_TX_BUF_LEN)
			{
				return NRC_RESPONSE_TOO_LONG;
			}

			UDS_TxBuf[out] = req[in];
			UDS_TxBuf[out + 1U] = req[in + 1U];
			out += 2U;
		}

		if (0 == didCfg->Read)
		{
			return NRC_REQUEST_OUT_OF_RANGE;
		}

		nrc = didCfg->Read(didCfg, &UDS_TxBuf[out],
		                   (unsigned int)(UDS_TX_BUF_LEN - out), &dataLen);
		if (0U != nrc)
		{
			return nrc;
		}

		if (dataLen > (UDS_TX_BUF_LEN - out))
		{
			return NRC_RESPONSE_TOO_LONG;
		}

		out += dataLen;
		in += 2U;
	}

	*rspLen = out;
	return 0U;
}

static unsigned char UDS_DcmWriteDataByIdentifier(const unsigned char *req,
                                                  unsigned int len,
                                                  unsigned int *rspLen)
{
	unsigned short did;
	unsigned int dataLen;
	const UDS_DcmDidType *didCfg;
	unsigned char nrc;

	if (len < 4U)
	{
		return NRC_INCORRECT_MESSAGE_LENGTH_OR_INVALID_FORMAT;
	}

	{
		unsigned char required = (UDS_DCM_SESSION_PROGRAMMING == UDS_DcmCtx.Session)
		    ? UDS_DCM_SECURITY_LEVEL2 : UDS_DCM_SECURITY_LEVEL1;

		if (0U == (UDS_DcmCtx.SecurityUnlocked & required))
		{
			return (unsigned char)NRC_SECURITY_ACCESS_DENIED;
		}
	}

	did = (unsigned short)(((unsigned short)req[1] << 8) | req[2]);
	didCfg = UDS_DcmFindDid(did);

	if (0 == didCfg)
	{
		return NRC_REQUEST_OUT_OF_RANGE;
	}

	if (0 == didCfg->Write)
	{
		return NRC_REQUEST_OUT_OF_RANGE;
	}

	dataLen = len - 3U;
	nrc = didCfg->Write(didCfg, &req[3], dataLen);

	if (0U != nrc)
	{
		return nrc;
	}

	UDS_TxBuf[1] = req[1];
	UDS_TxBuf[2] = req[2];
	*rspLen = 3U;
	return 0U;
}

/* ------------------------------------------------------------------------- */
/* Flash programming 0x34 / 0x36 / 0x37                                      */
/* ------------------------------------------------------------------------- */

static unsigned int UDS_DcmGetU32(const unsigned char *p)
{
	return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) |
	       ((unsigned int)p[2] << 8) | (unsigned int)p[3];
}

static unsigned char UDS_DcmRequestDownload(const unsigned char *req,
                                            unsigned int len,
                                            unsigned int *rspLen)
{
	unsigned int addr;
	unsigned int size;
	unsigned int maxBlock = 0U;
	unsigned char nrc;

	if (len < 11U)
	{
		return NRC_INCORRECT_MESSAGE_LENGTH_OR_INVALID_FORMAT;
	}

	/* addressAndLengthFormatIdentifier = 0x44: 4-byte address, 4-byte size. */
	if (0x44U != req[2])
	{
		return NRC_INCORRECT_MESSAGE_LENGTH_OR_INVALID_FORMAT;
	}

	addr = UDS_DcmGetU32(&req[3]);
	size = UDS_DcmGetU32(&req[7]);

	nrc = BootProg_RequestDownload(addr, size, &maxBlock);
	if (0U != nrc)
	{
		return nrc;
	}

	/* lengthFormatIdentifier = 0x20: the following maxNumberOfBlockLength is
	 * two bytes long. */
	UDS_TxBuf[1] = 0x20U;
	UDS_TxBuf[2] = (unsigned char)(maxBlock >> 8);
	UDS_TxBuf[3] = (unsigned char)maxBlock;
	*rspLen = 4U;
	return 0U;
}

static unsigned char UDS_DcmTransferData(const unsigned char *req,
                                         unsigned int len,
                                         unsigned int *rspLen)
{
	unsigned char nrc;

	if (len < 2U)
	{
		return NRC_INCORRECT_MESSAGE_LENGTH_OR_INVALID_FORMAT;
	}

	nrc = BootProg_TransferData(req[1], &req[2], len - 2U);
	if (0U != nrc)
	{
		return nrc;
	}

	UDS_TxBuf[1] = req[1];
	*rspLen = 2U;
	return 0U;
}

static unsigned char UDS_DcmTransferExit(const unsigned char *req,
                                         unsigned int len,
                                         unsigned int *rspLen)
{
	unsigned char nrc;

	(void)req;
	(void)len;

	/* 77 <SHA-256:32>: the digest is computed by the HSE over the image as
	 * it is stored (ITCM / backup area).  The tester compares it with the
	 * SHA-256 of the file it sent (and repeats it in 31 01 02 02). */
	nrc = BootProg_TransferExit(&UDS_TxBuf[1]);
	if (0U != nrc)
	{
		return nrc;
	}

	*rspLen = 1U + HSE_HASH_SHA256_SIZE;
	return 0U;
}

/* ------------------------------------------------------------------------- */
/* Routine control 0x31                                                      */
/* ------------------------------------------------------------------------- */

/*
 * 例程控制约定（0x0202 / 0x0203 / 0xFF00 / 0xFF01 / 0xFF02）：
 *
 *   71 <subFn> <RID> <status>
 *
 * status = 0 表示成功，非 0 表示失败原因（BOOT_ST_xxx，见 Boot_Program.h）。
 * 例程执行失败不再回 NRC 0x22；只有报文长度、子功能、例程号、安全等级等
 * 协议层错误仍回对应 NRC。0xFF00/0xFF02 每轮询一次返回 0x78，最终仍回正响应。
 */

/* 0xFF00: erase [address, address + length), one 8 KB sector per DCM poll.
 * Returning 0x78 makes the DCM call this routine again on its next poll. */
static unsigned char UDS_DcmRoutineErase(const unsigned char *req, unsigned int len,
                                         unsigned int *status)
{
	unsigned int addr;
	unsigned int size;
	unsigned char more = 0U;

	if ((len >= 13U) && (0x44U == req[4]))
	{
		/* QCC 04.171: 31 01 FF 00 44 <address:4> <size:4> */
		addr = UDS_DcmGetU32(&req[5]);
		size = UDS_DcmGetU32(&req[9]);
	}
	else if (len >= 12U)
	{
		addr = UDS_DcmGetU32(&req[4]);
		size = UDS_DcmGetU32(&req[8]);
	}
	else
	{
		return NRC_INCORRECT_MESSAGE_LENGTH_OR_INVALID_FORMAT;
	}

	/* 擦除结果通过 status 返回，例程本身始终回正响应。 */
	(void)BootProg_EraseStep(addr, size, status, &more);

	if (0U != more)
	{
		return (unsigned char)NRC_REQUEST_CORRECTLY_RECEIVED_RESPONSE_PENDING;
	}

	return 0U;
}

/* 0xFF01: programming dependency check (APP + flash driver).
 * The APP version policy is not evaluated when the last download was the
 * flash driver (fs): fs carries no APP version information. */
static unsigned char UDS_DcmRoutineDependency(const unsigned char *req,
                                              unsigned int len,
                                              unsigned int *status)
{
	(void)req;
	(void)len;

	return BootProg_DependencyCheck(status);
}

/* 0xFF02: copy the APP from the backup area to the run area, 8 KB per call.
 * Returning 0x78 makes the DCM call this routine again on its next poll. */
extern unsigned char BSW_stUpdateVerValid;
static unsigned char UDS_DcmRoutineCopy(const unsigned char *req, unsigned int len,
                                        unsigned int *status)
{
	unsigned char more = 0U;

	(void)req;
	(void)len;

	if (0U == BSW_stUpdateVerValid)
	{
		*status = BOOT_ST_COPY_NOT_CHECKED;
		return 0U;
	}

	(void)BootProg_CopyStep(status, &more);

	if (0U != more)
	{
		return (unsigned char)NRC_REQUEST_CORRECTLY_RECEIVED_RESPONSE_PENDING;
	}

	if (0U == *status)
	{
		/* Programming complete: do not leave the ITCM driver executable. */
		BootProg_DestroyFlash();
	}

	return 0U;
}

/* 0x0202: always answers positively; the result is the status byte.
 * Request: 31 01 02 02 <SHA-256:32> (HSE SHA-256 of the downloaded image). */
static unsigned char UDS_DcmRoutineIntegrity(const unsigned char *req, unsigned int len,
                                              unsigned int *status)
{
	if (len < (4U + HSE_HASH_SHA256_SIZE))
	{
		return (unsigned char)NRC_INCORRECT_MESSAGE_LENGTH_OR_INVALID_FORMAT;
	}

	return BootProg_CheckIntegrity(&req[4], status);
}

static unsigned char UDS_DcmRoutinePrecondition(const unsigned char *req, unsigned int len,
                                                unsigned int *status)
{
	(void)req;
	(void)len;

	UDS_DcmPreconditionOk = 1U;
	*status = 0U;
	return 0U;
}

static unsigned char UDS_DcmRoutineLoadHseKeys(const unsigned char *req, unsigned int len,
                                               unsigned int *status)
{
	(void)req;
	(void)len;

	/* 0x31 01 AA01：格式化 HSE 密钥目录，并把两个 AES-128 密钥导入
	 * NVM 密钥槽（group 0 slot 0/1）。密钥掉电保持，后续 0x27 直接使用。
	 * 参考 Chery C0V bootloader 的 HSE_Crypto_Init 流程。 */
	*status = (0U == HseAes_ProgramKeys()) ? 0U : 1U;
	return 0U;
}

const UDS_DcmRoutineType UDS_DcmRoutines[] =
{
	{0x0202U, UDS_DcmRoutineIntegrity, 0, 0},
	{0x0203U, UDS_DcmRoutinePrecondition, 0, 0},
	{0xFF00U, UDS_DcmRoutineErase, 0, 0},
	{0xFF01U, UDS_DcmRoutineDependency, 0, 0},
	{0xFF02U, UDS_DcmRoutineCopy, 0, 0},
	{0xAA01U, UDS_DcmRoutineLoadHseKeys, 0, 0}
};

const unsigned int UDS_DcmRoutineCount =
	(unsigned int)(sizeof(UDS_DcmRoutines) / sizeof(UDS_DcmRoutines[0]));

static unsigned char UDS_DcmRoutineControl(const unsigned char *req, unsigned int len, unsigned int *rspLen)
{
	unsigned short routineId;
	unsigned char subFn;
	unsigned int status = 0U;
	unsigned char nrc = 0U;
	unsigned int i;
	const UDS_DcmRoutineType *routine = 0;

	if (len < 4U)
	{
		return NRC_INCORRECT_MESSAGE_LENGTH_OR_INVALID_FORMAT;
	}

	subFn = (unsigned char)(req[1] & 0x7FU);
	routineId = (unsigned short)(((unsigned short)req[2] << 8) | req[3]);

	for (i = 0U; i < UDS_DcmRoutineCount; i++)
	{
		if (UDS_DcmRoutines[i].RoutineId == routineId)
		{
			routine = &UDS_DcmRoutines[i];
			break;
		}
	}

	if (0 == routine)
	{
		return NRC_REQUEST_OUT_OF_RANGE;
	}

	if ((0x0203U != routineId) && (0xAA01U != routineId))
	{
		unsigned char required = (UDS_DCM_SESSION_PROGRAMMING == UDS_DcmCtx.Session)
		    ? UDS_DCM_SECURITY_LEVEL2 : UDS_DCM_SECURITY_LEVEL1;

		if (0U == (UDS_DcmCtx.SecurityUnlocked & required))
		{
			return (unsigned char)NRC_SECURITY_ACCESS_DENIED;
		}
	}

	if (0x01U == subFn)
	{
		if (0 == routine->Start)
		{
			return NRC_SUBFUNCTION_NOT_SUPPORTED;
		}
		nrc = routine->Start(req, len, &status);
	}
	else if (0x02U == subFn)
	{
		if (0 == routine->Stop)
		{
			return NRC_SUBFUNCTION_NOT_SUPPORTED;
		}
		nrc = routine->Stop(req, len, &status);
	}
	else if (0x03U == subFn)
	{
		if (0 == routine->Result)
		{
			return NRC_SUBFUNCTION_NOT_SUPPORTED;
		}
		nrc = routine->Result(req, len, &status);
	}
	else
	{
		return NRC_SUBFUNCTION_NOT_SUPPORTED;
	}

	if (0U != nrc)
	{
		return nrc;
	}

	UDS_TxBuf[1] = subFn;
	UDS_TxBuf[2] = req[2];
	UDS_TxBuf[3] = req[3];
	UDS_TxBuf[4] = (unsigned char)status;
	*rspLen = 5U;
	return 0U;
}

static unsigned char UDS_DcmControlDtcSetting(const unsigned char *req,
                                              unsigned int len,
                                              unsigned int *rspLen)
{
	unsigned char subFn;

	if (len < 2U)
	{
		return (unsigned char)NRC_INCORRECT_MESSAGE_LENGTH_OR_INVALID_FORMAT;
	}

	subFn = (unsigned char)(req[1] & 0x7FU);
	if ((0x01U != subFn) && (0x02U != subFn))
	{
		return (unsigned char)NRC_SUBFUNCTION_NOT_SUPPORTED;
	}

	UDS_TxBuf[1] = subFn;
	*rspLen = 2U;
	return 0U;
}

static unsigned char UDS_DcmCommunicationControl(const unsigned char *req,
                                                 unsigned int len,
                                                 unsigned int *rspLen)
{
	unsigned char subFn;

	if (len < 3U)
	{
		return (unsigned char)NRC_INCORRECT_MESSAGE_LENGTH_OR_INVALID_FORMAT;
	}

	subFn = (unsigned char)(req[1] & 0x7FU);
	if ((0x00U != subFn) && (0x03U != subFn))
	{
		return (unsigned char)NRC_SUBFUNCTION_NOT_SUPPORTED;
	}

	if (0x03U != req[2])
	{
		return (unsigned char)NRC_REQUEST_OUT_OF_RANGE;
	}


	UDS_TxBuf[1] = subFn;
	*rspLen = 2U;
	return 0U;
}

/* ------------------------------------------------------------------------- */
/* Configuration tables                                                      */
/* ------------------------------------------------------------------------- */

/*
 * Service configuration.  Each row is one SID/sub-function combination.
 *
 *   SessionMask   : sessions in which the entry is available
 *                   (UDS_DCM_SESSION_MASK_xxx).
 *   SecurityLevel : security level that must be unlocked before the handler
 *                   is called; UDS_DCM_SECURITY_NONE means no security.
 *   AddrMask      : addressing modes in which the entry is answered.
 *                   A functional request for a PHYSICAL-only entry is
 *                   silently ignored (no positive and no negative response).
 *
 * The DCM core turns this table into the following NRCs:
 *
 *   SID not listed                                  -> 0x11
 *   sub-function not listed                         -> 0x12
 *   sub-function listed but not in active session   -> 0x7E
 *   service without sub-function not in session     -> 0x7F
 *   security level not unlocked                     -> 0x33
 *
 * Current diagnostic requirements:
 *
 *   0x10 DiagnosticSessionControl   all sessions          no security
 *   0x11 ECUReset                   all sessions          no security
 *   0x22 ReadDataByIdentifier       all sessions          no security
 *   0x27 SecurityAccess             all sessions          no security
 *   0x2E WriteDataByIdentifier      Extended, Programming Level 1
 *   0x31 RoutineControl             Extended, Programming Level 1
 *                                   physical addressing only
 *
 * Adjust the mask/level fields below when the diagnostic specification
 * changes; the DCM core does not need to be modified.
 */
const UDS_DcmServiceType UDS_DcmServices[] =
{
	/* SID   SubFn  SessionMask                                        SecurityLevel           AddrMask                Handler */
	{0x10U, 0x01U, UDS_DCM_SESSION_MASK_ALL,                           UDS_DCM_SECURITY_NONE,  UDS_DCM_ADDR_MASK_ALL,  UDS_DcmSessionDefault},
	{0x10U, 0x02U, UDS_DCM_SESSION_MASK_EXTENDED,                      UDS_DCM_SECURITY_NONE,  UDS_DCM_ADDR_MASK_PHYSICAL, UDS_DcmSessionProgramming},
	{0x10U, 0x03U, (UDS_DCM_SESSION_MASK_DEFAULT | UDS_DCM_SESSION_MASK_EXTENDED), UDS_DCM_SECURITY_NONE, UDS_DCM_ADDR_MASK_ALL, UDS_DcmSessionExtended},

	{0x11U, 0x01U, (UDS_DCM_SESSION_MASK_EXTENDED | UDS_DCM_SESSION_MASK_PROGRAMMING), UDS_DCM_SECURITY_NONE, UDS_DCM_ADDR_MASK_ALL, UDS_DcmResetHard},
	{0x11U, 0x02U, (UDS_DCM_SESSION_MASK_EXTENDED | UDS_DCM_SESSION_MASK_PROGRAMMING), UDS_DCM_SECURITY_NONE, UDS_DCM_ADDR_MASK_ALL, UDS_DcmResetKeyOffOn},
	{0x11U, 0x03U, (UDS_DCM_SESSION_MASK_EXTENDED | UDS_DCM_SESSION_MASK_PROGRAMMING), UDS_DCM_SECURITY_NONE, UDS_DCM_ADDR_MASK_ALL, UDS_DcmResetSoft},

	{0x22U, UDS_DCM_NO_SUBFN, UDS_DCM_SESSION_MASK_ALL,                UDS_DCM_SECURITY_NONE,  UDS_DCM_ADDR_MASK_ALL,  UDS_DcmReadDataByIdentifier},

	{0x2EU, UDS_DCM_NO_SUBFN, (UDS_DCM_SESSION_MASK_EXTENDED |
	                          UDS_DCM_SESSION_MASK_PROGRAMMING),       UDS_DCM_SECURITY_NONE,  UDS_DCM_ADDR_MASK_ALL,  UDS_DcmWriteDataByIdentifier},

	{0x34U, UDS_DCM_NO_SUBFN, UDS_DCM_SESSION_MASK_PROGRAMMING,        UDS_DCM_SECURITY_LEVEL2, UDS_DCM_ADDR_MASK_PHYSICAL, UDS_DcmRequestDownload},
	{0x36U, UDS_DCM_NO_SUBFN, UDS_DCM_SESSION_MASK_PROGRAMMING,        UDS_DCM_SECURITY_LEVEL2, UDS_DCM_ADDR_MASK_PHYSICAL, UDS_DcmTransferData},
	{0x37U, UDS_DCM_NO_SUBFN, UDS_DCM_SESSION_MASK_PROGRAMMING,        UDS_DCM_SECURITY_LEVEL2, UDS_DCM_ADDR_MASK_PHYSICAL, UDS_DcmTransferExit},

	{0x27U, 0x01U, UDS_DCM_SESSION_MASK_EXTENDED,                      UDS_DCM_SECURITY_NONE,  UDS_DCM_ADDR_MASK_ALL,  UDS_DcmSeedRequest},
	{0x27U, 0x02U, UDS_DCM_SESSION_MASK_EXTENDED,                      UDS_DCM_SECURITY_NONE,  UDS_DCM_ADDR_MASK_ALL,  UDS_DcmKeyRequest},
	{0x27U, 0x03U, UDS_DCM_SESSION_MASK_PROGRAMMING,                   UDS_DCM_SECURITY_NONE,  UDS_DCM_ADDR_MASK_ALL,  UDS_DcmSeedRequest},
	{0x27U, 0x04U, UDS_DCM_SESSION_MASK_PROGRAMMING,                   UDS_DCM_SECURITY_NONE,  UDS_DCM_ADDR_MASK_ALL,  UDS_DcmKeyRequest},

	{0x31U, 0x01U, UDS_DCM_SESSION_MASK_ALL,                 UDS_DCM_SECURITY_NONE, UDS_DCM_ADDR_MASK_PHYSICAL, UDS_DcmRoutineControl},
	{0x31U, 0x02U, (UDS_DCM_SESSION_MASK_EXTENDED |
	                UDS_DCM_SESSION_MASK_PROGRAMMING),                 UDS_DCM_SECURITY_NONE, UDS_DCM_ADDR_MASK_PHYSICAL, UDS_DcmRoutineControl},
	{0x31U, 0x03U, (UDS_DCM_SESSION_MASK_EXTENDED |
	                UDS_DCM_SESSION_MASK_PROGRAMMING),                 UDS_DCM_SECURITY_NONE, UDS_DCM_ADDR_MASK_PHYSICAL, UDS_DcmRoutineControl},

	{0x3EU, 0x00U, UDS_DCM_SESSION_MASK_ALL,                           UDS_DCM_SECURITY_NONE,  UDS_DCM_ADDR_MASK_ALL,  UDS_DcmKeepSession},

	{0x85U, 0x01U, UDS_DCM_SESSION_MASK_EXTENDED,                      UDS_DCM_SECURITY_NONE,  UDS_DCM_ADDR_MASK_ALL,  UDS_DcmControlDtcSetting},
	{0x85U, 0x02U, UDS_DCM_SESSION_MASK_EXTENDED,                      UDS_DCM_SECURITY_NONE,  UDS_DCM_ADDR_MASK_ALL,  UDS_DcmControlDtcSetting},

	{0x28U, 0x00U, UDS_DCM_SESSION_MASK_EXTENDED,                      UDS_DCM_SECURITY_NONE,  UDS_DCM_ADDR_MASK_ALL,  UDS_DcmCommunicationControl},
	{0x28U, 0x03U, UDS_DCM_SESSION_MASK_EXTENDED,                      UDS_DCM_SECURITY_NONE,  UDS_DCM_ADDR_MASK_ALL,  UDS_DcmCommunicationControl},
};

const unsigned int UDS_DcmServiceCount =
	(unsigned int)(sizeof(UDS_DcmServices) / sizeof(UDS_DcmServices[0]));

void UDS_DcmCfgInit(void)
{
	/* Add application-specific initialization here. */
}

void UDS_DcmCfgPoll(void)
{
	/* Leaving programming/extended session (10 01) returns to the APP. */
	Boot_ProcessJumpRequest();
}

void UDS_DCM_ResetHook(void);

void UDS_DcmCfgSessionTimeout(void)
{
	/* S3 timeout in programming/extended session: return to the APP. */
	if ((UDS_DCM_SESSION_PROGRAMMING == UDS_DcmCtx.Session) ||
	    (UDS_DCM_SESSION_EXTENDED == UDS_DcmCtx.Session))
	{
		UDS_DCM_ResetHook();
	}
}

/*
 * Default reset implementation.  Override in application code if a different
 * reset sequence is required.
 */
__attribute__((weak)) void UDS_DCM_ResetHook(void)
{
	BootProg_DestroyFlash();
	Mcu_PerformReset();
}



