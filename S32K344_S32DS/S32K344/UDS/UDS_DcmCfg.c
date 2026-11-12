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
#include "ASW_CanCallback.h"
#include "UserOS.h"
#include "Mcu.h"
#include "Gpt.h"
#include "Can_43_FLEXCAN.h"
#include "SchM_Can_43_FLEXCAN.h"
#include "Boot_Shared.h"
#include "App_Info.h"
#include "Nvm.h"
#include "NvmApp.h"
#include <string.h>
#include "hse_trng.h"
#include "hse_aes.h"      /* HSE 硬件 TRNG，供 UDS 0x27 取随机种子 */

UDS_DcmContextType UDS_DcmCtx;

/*
 * 0x28 CommunicationControl state.
 *
 * One entry per ISO 14229-1 communication type.  The type value is used as
 * a bit mask in this table (bit 0 = normal communication, bit 1 = network
 * management), so communicationType 0x03 updates both entries in one loop.
 */
#define UDS_DCM_COMM_TYPE_NORMAL   0x01U
#define UDS_DCM_COMM_TYPE_NM       0x02U
#define UDS_DCM_COMM_TYPE_ALL      (UDS_DCM_COMM_TYPE_NORMAL | UDS_DCM_COMM_TYPE_NM)
#define UDS_DCM_COMM_COUNT         2U

static unsigned char UDS_DcmCommRxEnabled[UDS_DCM_COMM_COUNT] = {1U, 1U};
static unsigned char UDS_DcmCommTxEnabled[UDS_DCM_COMM_COUNT] = {1U, 1U};

static void UDS_DcmApplyCommunicationControl(void)
{
	ASW_SetNormalCommEnabled(UDS_DcmCommRxEnabled[0], UDS_DcmCommTxEnabled[0]);
	ASW_SetNmCommEnabled(UDS_DcmCommRxEnabled[1], UDS_DcmCommTxEnabled[1]);
}

static void UDS_DcmResetCommunicationControl(void)
{
	unsigned int i;

	for (i = 0U; i < UDS_DCM_COMM_COUNT; i++)
	{
		UDS_DcmCommRxEnabled[i] = 1U;
		UDS_DcmCommTxEnabled[i] = 1U;
	}

	UDS_DcmApplyCommunicationControl();
}

/* UDS 0x27：16 字节种子 + AES128 预期密钥（文件内静态，避免改上下文结构体） */
static unsigned char UDS_Seed16[16];
static unsigned char UDS_ExpectedKey[16];

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

	/* Returning to the default session restores the default communication
	 * state, so a tester that switched communication off cannot leave the
	 * ECU silent after the session ends. */
	if (UDS_DCM_SESSION_DEFAULT == session)
	{
		UDS_DcmResetCommunicationControl();
	}

	UDS_TxBuf[1] = (unsigned char)session;
	*rspLen = 2U;
	return 0U;
}

static unsigned char UDS_DcmSessionDefault(const unsigned char *req, unsigned int len, unsigned int *rspLen)
{
	(void)req;
	(void)len;
	return UDS_DcmSetSession(UDS_DCM_SESSION_DEFAULT, rspLen);
}

static unsigned char BootJumpRequest = 0U;
static unsigned int BootJumpTick = 0U;
static unsigned char UDS_DcmPreconditionOk = 0U;

static unsigned char UDS_DcmSessionProgramming(const unsigned char *req, unsigned int len, unsigned int *rspLen)
{
	(void)req;
	(void)len;
	(void)rspLen;

	/* QCC 04.171: 10 02 is only accepted after 31 01 02 03. */
	if (0U == UDS_DcmPreconditionOk)
	{
		return (unsigned char)NRC_CONDITIONS_NOT_CORRECT;
	}

	/* 0x10 0x02: answer 0x78 and let UDS_DcmCfgPoll() (DCM task context)
	 * write the magic and reset the ECU.  Doing the reset in the DCM task
	 * avoids re-entering the CAN driver from the reception callback. */
	UDS_CanSendNRC(0x10U, NRC_REQUEST_CORRECTLY_RECEIVED_RESPONSE_PENDING);

	UDS_DcmPreconditionOk = 0U;
	BootJumpTick = BSW_GetSystemTick();
	BootJumpRequest = 1U;

	return (unsigned char)UDS_DCM_NRC_NO_RESPONSE;
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
	if (0U == (UDS_DcmCtx.SecurityUnlocked & UDS_DCM_SECURITY_LEVEL1))
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
 * Stored in NVM block 0 with magic + checksum (see NvmApp.h):
 *   22 F190 returns the VIN, or 17 x 0x00 when no valid record exists,
 *   2E F190 stores it once and commits the block immediately.
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

	memset(buf, 0xFF, 9U);
	*outLen = 9U;
	return 0U;
}

/*
 * Version / identification DIDs.
 *
 * Every DID returns one 16-byte App_Info block read from a fixed flash
 * address, so the answer is identical no matter whether the APP or the BOOT
 * is running (the bootloader uses the same table):
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
	{0xF19EU, 0x00448140UL},
	{0xFE00U, 0x00400100UL},//BOOT 软件版本号
	{0xFE01U, 0x00448100UL},//APP 软件版本号
	{0xFE02U, 0x00448110UL},//硬件版型版本号
	{0xFE03U, 0x00448120UL},//ECU 型号
	{0xFE04U, 0x00448160UL},//APP 软件编译日期
	{0xFE05U, 0x00448170UL},//APP 软件编译时间
	{0xFE06U, 0x00400160UL},//BOOT 软件编译日期
	{0xFE07U, 0x00400170UL}//BOOT 软件编译时间
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

	/* The write is refused when F190 was already programmed (see
	 * Nvm_F190_Write): all bytes must still be 0xFF. */
	if (E_OK != Nvm_F190_Write(buf))
	{
		return NRC_CONDITIONS_NOT_CORRECT;
	}

	return 0U;
}

const UDS_DcmDidType UDS_DcmDids[] =
{
	{0xF190U, UDS_DcmF190Read, UDS_DcmF190Write},
	{0xF184U, UDS_DcmF184Read, 0},
	{0xFE00U, UDS_DcmFixedDidRead, 0},
	{0xFE01U, UDS_DcmFixedDidRead, 0},
	{0xFE02U, UDS_DcmFixedDidRead, 0},
	{0xFE03U, UDS_DcmFixedDidRead, 0},
	{0xFE04U, UDS_DcmFixedDidRead, 0},
	{0xFE05U, UDS_DcmFixedDidRead, 0},
	{0xFE06U, UDS_DcmFixedDidRead, 0},
	{0xFE07U, UDS_DcmFixedDidRead, 0},
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

	if (0U == (UDS_DcmCtx.SecurityUnlocked & UDS_DCM_SECURITY_LEVEL1))
	{
		return (unsigned char)NRC_SECURITY_ACCESS_DENIED;
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
/* Routine control 0x31                                                      */
/* ------------------------------------------------------------------------- */

static unsigned char UDS_DcmRoutineStartDemo(const unsigned char *req, unsigned int len,
                                             unsigned int *status)
{
	(void)req;
	(void)len;
	UDS_DcmPreconditionOk = 1U;
	*status = 0x00U;
	return 0U;
}

static unsigned char UDS_DcmRoutineStopDemo(const unsigned char *req, unsigned int len,
                                            unsigned int *status)
{
	(void)req;
	(void)len;
	*status = 0x00U;
	return 0U;
}

static unsigned char UDS_DcmRoutineResultDemo(const unsigned char *req, unsigned int len,
                                              unsigned int *status)
{
	(void)req;
	(void)len;
	*status = 0x00U;
	return 0U;
}

const UDS_DcmRoutineType UDS_DcmRoutines[] =
{
	{0x0203U, UDS_DcmRoutineStartDemo, UDS_DcmRoutineStopDemo, UDS_DcmRoutineResultDemo}
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

	if (0x0203U != routineId)
	{
		if (0U == (UDS_DcmCtx.SecurityUnlocked & UDS_DCM_SECURITY_LEVEL1))
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

static unsigned char UDS_DcmCommunicationControl(const unsigned char *req,
                                                 unsigned int len,
                                                 unsigned int *rspLen)
{
	unsigned char subFn;
	unsigned char commType;
	unsigned char rxEnabled;
	unsigned char txEnabled;
	unsigned int i;

	/* 28 <sub-function> <communicationType>: exactly three bytes. */
	if (len != 3U)
	{
		return (unsigned char)NRC_INCORRECT_MESSAGE_LENGTH_OR_INVALID_FORMAT;
	}

	subFn = (unsigned char)(req[1] & 0x7FU);
	commType = req[2];

	/* Sub-functions 0x00..0x03 control Rx/Tx.  The enhanced-address
	 * variants 0x04/0x05 are optional and need an additional address
	 * parameter, so they are not supported here. */
	if (subFn > 0x03U)
	{
		return (unsigned char)NRC_SUBFUNCTION_NOT_SUPPORTED;
	}

	/* 0x01 normal, 0x02 network management, 0x03 both.  Everything else
	 * (including the reserved 0x04..0xFF range) is out of range. */
	if ((commType < UDS_DCM_COMM_TYPE_NORMAL) || (commType > UDS_DCM_COMM_TYPE_ALL))
	{
		return (unsigned char)NRC_REQUEST_OUT_OF_RANGE;
	}

	/*
	 * ISO 14229-1 sub-function encoding:
	 *   0x00 enableRxAndTx
	 *   0x01 enableRxAndDisableTx
	 *   0x02 disableRxAndEnableTx
	 *   0x03 disableRxAndTx
	 */
	switch (subFn)
	{
	case 0x00U:
		rxEnabled = 1U;
		txEnabled = 1U;
		break;

	case 0x01U:
		rxEnabled = 1U;
		txEnabled = 0U;
		break;

	case 0x02U:
		rxEnabled = 0U;
		txEnabled = 1U;
		break;

	default:
		rxEnabled = 0U;
		txEnabled = 0U;
		break;
	}

	for (i = 0U; i < UDS_DCM_COMM_COUNT; i++)
	{
		if (0U != (commType & (unsigned char)(1U << i)))
		{
			UDS_DcmCommRxEnabled[i] = rxEnabled;
			UDS_DcmCommTxEnabled[i] = txEnabled;
		}
	}

	UDS_DcmApplyCommunicationControl();

	/* Positive response: 68 <sub-function> <communicationType>. */
	UDS_TxBuf[1] = subFn;
	UDS_TxBuf[2] = commType;
	*rspLen = 3U;
	return 0U;
}

static unsigned char UDS_DcmDtcSettingOff;

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

	UDS_DcmDtcSettingOff = (0x02U == subFn) ? 1U : 0U;

	UDS_TxBuf[1] = subFn;
	*rspLen = 2U;
	return 0U;
}

static unsigned char UDS_DcmClearDiagnosticInformation(const unsigned char *req,
                                                       unsigned int len,
                                                       unsigned int *rspLen)
{
	(void)req;

	if (len < 4U)
	{
		return (unsigned char)NRC_INCORRECT_MESSAGE_LENGTH_OR_INVALID_FORMAT;
	}

	*rspLen = 1U;
	return 0U;
}

unsigned char UDS_DcmIsCommunicationDisabled(void)
{
	/* Historical meaning: normal application communication is disabled in
	 * both directions. */
	return ((0U == UDS_DcmCommRxEnabled[0]) &&
	        (0U == UDS_DcmCommTxEnabled[0])) ? 1U : 0U;
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
 *   0x28 CommunicationControl       Extended              no security
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
	{0x10U, 0x03U, UDS_DCM_SESSION_MASK_ALL,                           UDS_DCM_SECURITY_NONE,  UDS_DCM_ADDR_MASK_ALL,  UDS_DcmSessionExtended},

	{0x11U, 0x01U, UDS_DCM_SESSION_MASK_EXTENDED,                      UDS_DCM_SECURITY_NONE,  UDS_DCM_ADDR_MASK_ALL,  UDS_DcmResetHard},
	{0x11U, 0x02U, UDS_DCM_SESSION_MASK_EXTENDED,                      UDS_DCM_SECURITY_NONE,  UDS_DCM_ADDR_MASK_ALL,  UDS_DcmResetKeyOffOn},
	{0x11U, 0x03U, UDS_DCM_SESSION_MASK_EXTENDED,                      UDS_DCM_SECURITY_NONE,  UDS_DCM_ADDR_MASK_ALL,  UDS_DcmResetSoft},

	{0x22U, UDS_DCM_NO_SUBFN, UDS_DCM_SESSION_MASK_ALL,                UDS_DCM_SECURITY_NONE,  UDS_DCM_ADDR_MASK_ALL,  UDS_DcmReadDataByIdentifier},

	{0x28U, 0x00U, UDS_DCM_SESSION_MASK_EXTENDED,                      UDS_DCM_SECURITY_NONE,  UDS_DCM_ADDR_MASK_ALL,  UDS_DcmCommunicationControl},
	{0x28U, 0x01U, UDS_DCM_SESSION_MASK_EXTENDED,                      UDS_DCM_SECURITY_NONE,  UDS_DCM_ADDR_MASK_ALL,  UDS_DcmCommunicationControl},
	{0x28U, 0x02U, UDS_DCM_SESSION_MASK_EXTENDED,                      UDS_DCM_SECURITY_NONE,  UDS_DCM_ADDR_MASK_ALL,  UDS_DcmCommunicationControl},
	{0x28U, 0x03U, UDS_DCM_SESSION_MASK_EXTENDED,                      UDS_DCM_SECURITY_NONE,  UDS_DCM_ADDR_MASK_ALL,  UDS_DcmCommunicationControl},

	{0x2EU, UDS_DCM_NO_SUBFN, UDS_DCM_SESSION_MASK_EXTENDED,           UDS_DCM_SECURITY_NONE,  UDS_DCM_ADDR_MASK_ALL,  UDS_DcmWriteDataByIdentifier},

	{0x27U, 0x01U, UDS_DCM_SESSION_MASK_EXTENDED,                      UDS_DCM_SECURITY_NONE,  UDS_DCM_ADDR_MASK_ALL,  UDS_DcmSeedRequest},
	{0x27U, 0x02U, UDS_DCM_SESSION_MASK_EXTENDED,                      UDS_DCM_SECURITY_NONE,  UDS_DCM_ADDR_MASK_ALL,  UDS_DcmKeyRequest},
	{0x27U, 0x03U, UDS_DCM_SESSION_MASK_PROGRAMMING,                   UDS_DCM_SECURITY_NONE,  UDS_DCM_ADDR_MASK_ALL,  UDS_DcmSeedRequest},
	{0x27U, 0x04U, UDS_DCM_SESSION_MASK_PROGRAMMING,                   UDS_DCM_SECURITY_NONE,  UDS_DCM_ADDR_MASK_ALL,  UDS_DcmKeyRequest},

	{0x31U, 0x01U, UDS_DCM_SESSION_MASK_EXTENDED,                      UDS_DCM_SECURITY_NONE,  UDS_DCM_ADDR_MASK_PHYSICAL, UDS_DcmRoutineControl},
	{0x31U, 0x02U, UDS_DCM_SESSION_MASK_EXTENDED,                      UDS_DCM_SECURITY_NONE,  UDS_DCM_ADDR_MASK_PHYSICAL, UDS_DcmRoutineControl},
	{0x31U, 0x03U, UDS_DCM_SESSION_MASK_EXTENDED,                      UDS_DCM_SECURITY_NONE,  UDS_DCM_ADDR_MASK_PHYSICAL, UDS_DcmRoutineControl},

	{0x85U, 0x01U, UDS_DCM_SESSION_MASK_EXTENDED,                      UDS_DCM_SECURITY_NONE,  UDS_DCM_ADDR_MASK_ALL,  UDS_DcmControlDtcSetting},
	{0x85U, 0x02U, UDS_DCM_SESSION_MASK_EXTENDED,                      UDS_DCM_SECURITY_NONE,  UDS_DCM_ADDR_MASK_ALL,  UDS_DcmControlDtcSetting},

	{0x14U, UDS_DCM_NO_SUBFN, (UDS_DCM_SESSION_MASK_DEFAULT | UDS_DCM_SESSION_MASK_EXTENDED), UDS_DCM_SECURITY_NONE, UDS_DCM_ADDR_MASK_PHYSICAL, UDS_DcmClearDiagnosticInformation},

	{0x3EU, 0x00U, UDS_DCM_SESSION_MASK_ALL,                           UDS_DCM_SECURITY_NONE,  UDS_DCM_ADDR_MASK_ALL,  UDS_DcmKeepSession},
};

const unsigned int UDS_DcmServiceCount =
	(unsigned int)(sizeof(UDS_DcmServices) / sizeof(UDS_DcmServices[0]));

void UDS_DcmCfgInit(void)
{
	/* Start with all communication enabled. */
	UDS_DcmResetCommunicationControl();
}

void UDS_DcmCfgSessionTimeout(void)
{
	/* The application stays in the default session on an S3 timeout.  The
	 * session change also restores the default communication state, so a
	 * disabled ECU cannot stay silent forever. */
	UDS_DcmResetCommunicationControl();
}

void UDS_DcmCfgPoll(void)
{
	/* The APP->Boot request is completed here, in the DCM task context:
	 * write the magic, clean it to the SRAM and reset (app_jump_boot()). */
	if (0U != BootJumpRequest)
	{
		if (((BSW_GetSystemTick() - BootJumpTick) >= 100U) &&
		    (0U == UDS_CanTpIsTxBusy()))
		{
			BootJumpRequest = 0U;

			__asm volatile ("cpsid i" ::: "memory");

			/* Write and clean the cache line so the value is in the SRAM. */
			Boot_RamFlagSet(BOOT_RAM_REQUEST_PROGRAMMING);
			*(volatile unsigned int *)0xE000EF68UL = BOOT_RAM_FLAG_ADDR;
			__asm volatile ("dsb" ::: "memory");


			{
				volatile unsigned int d;
				for (d = 0U; d < 500000U; d++) { }
			}

			Mcu_PerformReset();
		}
	}
}
/*
 * Default reset implementation.  Override in application code if a different
 * reset sequence is required.
 */
__attribute__((weak)) void UDS_DCM_ResetHook(void)
{
	Mcu_PerformReset();
}



















