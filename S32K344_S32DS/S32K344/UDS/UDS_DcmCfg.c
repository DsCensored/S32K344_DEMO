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
#include "UDS_DEM.h"
#include "UDS_DTC.h"
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
#include "hse_host.h"     /* HSE GetAttr，供 UDS 0x22 FE08 */

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

		/* 0x85 02 must not survive the session: otherwise no DTC would ever
		 * be recorded again after the tester left the extended session. */
		UDS_DtcSetSettingOff(0U);
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

/* -------------------------------------------------------------------------
 * FE08 / FE09: HSE / MCU identification DIDs
 *
 *   FE08  HSE firmware version, returned as a zero padded ASCII string
 *         "major.minor.patch" in a 16-byte block (same layout as FE00..FE07).
 *   FE09  S32K3 MCU unique ID, 8 bytes read from the UTEST UID area at
 *         0x1B000040.  This value is programmed by NXP and does not depend
 *         on the HSE key catalog, so it also works before/without a SHE
 *         MASTER_ECU_KEY.
 *
 * Note: the HSE SHE Get ID service returns the 120-bit SHE UID, but it
 * requires a provisioned SHE MASTER_ECU_KEY (NVM catalog group 0 slot 0).
 * This project uses that catalog slot for the 0x27 AES keys, so SHE Get ID
 * answers HSE_SRV_RSP_KEY_INVALID and is not used here.
 * ---------------------------------------------------------------------- */
#define UDS_DCM_HSE_VERSION_LEN  16U
#define UDS_DCM_S32K3_UID_ADDR   0x1B000040UL
#define UDS_DCM_S32K3_UID_LEN    8U

static unsigned int UDS_DcmAppendDec(unsigned char *buf, unsigned int pos,
                                     unsigned int value)
{
	unsigned char digits[10];
	unsigned int count = 0U;

	do
	{
		digits[count] = (unsigned char)('0' + (value % 10U));
		count++;
		value /= 10U;
	} while (0U != value);

	while (0U != count)
	{
		count--;
		buf[pos] = digits[count];
		pos++;
	}

	return pos;
}

static unsigned char UDS_DcmHseVersionRead(const UDS_DcmDidType *did,
                                           unsigned char *buf,
                                           unsigned int maxLen,
                                           unsigned int *outLen)
{
	hseAttrFwVersion_t version;
	unsigned int pos = 0U;

	(void)did;

	if (maxLen < UDS_DCM_HSE_VERSION_LEN)
	{
		return NRC_RESPONSE_TOO_LONG;
	}

	if (HSE_SRV_RSP_OK != HSE_GetFwVersion(&version))
	{
		return NRC_CONDITIONS_NOT_CORRECT;
	}

	memset(buf, 0, UDS_DCM_HSE_VERSION_LEN);
	pos = UDS_DcmAppendDec(buf, pos, (unsigned int)version.majorVersion);
	buf[pos] = '.';
	pos++;
	pos = UDS_DcmAppendDec(buf, pos, (unsigned int)version.minorVersion);
	buf[pos] = '.';
	pos++;
	(void)UDS_DcmAppendDec(buf, pos, (unsigned int)version.patchVersion);

	*outLen = UDS_DCM_HSE_VERSION_LEN;
	return 0U;
}

static unsigned char UDS_DcmS32K3UidRead(const UDS_DcmDidType *did,
                                         unsigned char *buf,
                                         unsigned int maxLen,
                                         unsigned int *outLen)
{
	const unsigned char *uid = (const unsigned char *)UDS_DCM_S32K3_UID_ADDR;

	(void)did;

	if (maxLen < UDS_DCM_S32K3_UID_LEN)
	{
		return NRC_RESPONSE_TOO_LONG;
	}

	memcpy(buf, uid, UDS_DCM_S32K3_UID_LEN);
	*outLen = UDS_DCM_S32K3_UID_LEN;
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
 *
 * FE08 is read from the HSE at run time; FE09 is the 8-byte MCU UID
 * programmed by NXP in the UTEST area at 0x1B000040.
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

/*
 * 0xABCC：HSE AB Swap 当前运行分区。
 *
 *   22 AB CC                -> 62 AB CC <bank>
 *     bank 0x00 = A 区（低地址：BOOT 0x00400000 / APP 0x00448000）
 *          0x01 = B 区（高地址：BOOT 0x00600000 / APP 0x00648000）
 *          0xFF = 无法判断（DCM 扫描未完成，或本机不是 AB Swap 配置）
 *
 * 只读 DCMSTAT，不依赖 NVM，所以切区复位后立刻可读。
 */
#define ASW_DCMSTAT_ADDR        (0x402AC000UL)
#define ASW_DCMSTAT_DCMDONE     (1UL << 0U)
#define ASW_DCMSTAT_DCMOTAA     (1UL << 16U)
#define ASW_DCMSTAT_DCMOTAR     (1UL << 17U)
#define ASW_BANK_A              ('A')
#define ASW_BANK_B              ('B')
#define ASW_BANK_UNKNOWN        (0xFFU)

static unsigned int ASW_GetDcmStatus(void)
{
	return *(volatile unsigned int *)ASW_DCMSTAT_ADDR;
}

static unsigned char ASW_GetActiveBank(void)
{
	unsigned int status = ASW_GetDcmStatus();

	/* DCMOTAA / DCMOTAR are only valid after the DCM scan completed. */
	if (0U == (status & ASW_DCMSTAT_DCMDONE))
	{
		return ASW_BANK_UNKNOWN;
	}

	/* AB swap not active (full-memory HSE firmware or OTA disabled): the
	 * device has no A/B partition to report. */
	if (0U == (status & ASW_DCMSTAT_DCMOTAA))
	{
		return ASW_BANK_UNKNOWN;
	}

	/* AN13465: DCMOTAR 0 = low address (A), 1 = high address (B). */
	return (0U != (status & ASW_DCMSTAT_DCMOTAR)) ? ASW_BANK_B : ASW_BANK_A;
}
static unsigned char UDS_DcmAbccRead(const UDS_DcmDidType *did,
                                     unsigned char *buf,
                                     unsigned int maxLen,
                                     unsigned int *outLen)
{
	(void)did;

	if (maxLen < 1U)
	{
		return NRC_RESPONSE_TOO_LONG;
	}

	buf[0] = ASW_GetActiveBank();
	*outLen = 1U;
	return 0U;
}

const UDS_DcmDidType UDS_DcmDids[] =
{
	{0xF190U, UDS_DcmF190Read, UDS_DcmF190Write},
	{0xF184U, UDS_DcmF184Read, 0},
	{0xABCCU, UDS_DcmAbccRead, 0},
	{0xFE00U, UDS_DcmFixedDidRead, 0},
	{0xFE01U, UDS_DcmFixedDidRead, 0},
	{0xFE02U, UDS_DcmFixedDidRead, 0},
	{0xFE03U, UDS_DcmFixedDidRead, 0},
	{0xFE04U, UDS_DcmFixedDidRead, 0},
	{0xFE05U, UDS_DcmFixedDidRead, 0},
	{0xFE06U, UDS_DcmFixedDidRead, 0},
	{0xFE07U, UDS_DcmFixedDidRead, 0},
	{0xFE08U, UDS_DcmHseVersionRead, 0},
	{0xFE09U, UDS_DcmS32K3UidRead, 0},
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

static unsigned char UDS_DcmControlDtcSetting(const unsigned char *req,
                                              unsigned int len,
                                              unsigned int *rspLen)
{
	unsigned char subFn;

	if (len < 2U)
	{
		return (unsigned char)NRC_INCORRECT_MESSAGE_LENGTH_OR_INVALID_FORMAT;
	}

	/* ISO 14229-1: 0x01 on (enable DTC setting), 0x02 off (freeze it). */
	subFn = (unsigned char)(req[1] & 0x7FU);
	if ((0x01U != subFn) && (0x02U != subFn))
	{
		return (unsigned char)NRC_SUBFUNCTION_NOT_SUPPORTED;
	}

	/* The DEM keeps running the monitors for the application but stops
	 * updating and storing the DTC status bytes while DTC setting is off. */
	UDS_DtcSetSettingOff((0x02U == subFn) ? 1U : 0U);

	UDS_TxBuf[1] = subFn;
	*rspLen = 2U;
	return 0U;
}

static unsigned char UDS_DcmClearDiagnosticInformation(const unsigned char *req,
                                                       unsigned int len,
                                                       unsigned int *rspLen)
{
	unsigned int group;
	unsigned char res;

	/* 14 <groupOfDTC 3 bytes> */
	if (len < 4U)
	{
		return (unsigned char)NRC_INCORRECT_MESSAGE_LENGTH_OR_INVALID_FORMAT;
	}

	group = ((unsigned int)req[1] << 16) |
	        ((unsigned int)req[2] << 8) |
	         (unsigned int)req[3];

	res = UDS_DtcClearGroup(group);

	if (DEM_OK != res)
	{
		if (DEM_WRITE_FAILED == res)
		{
			/* ISO 14229-1, 12.2.4: the memory could not be written. */
			return (unsigned char)NRC_GENERAL_PROGRAMMING_FAILURE;
		}

		return (DEM_NOT_READY == res) ? (unsigned char)NRC_CONDITIONS_NOT_CORRECT :
		                                (unsigned char)NRC_REQUEST_OUT_OF_RANGE;
	}

	/* The clear sets the "not completed" status bits again: let the running
	 * monitors report their next result so the bits clear as specified in
	 * Table D.7 / D.9. */
	UDS_DemRestartTestReport();

	/* Positive response is the SID alone (the DCM core sends at least one
	 * byte), no parameter is echoed. */
	*rspLen = 1U;
	return 0U;
}

/* ------------------------------------------------------------------------- */
/* Read DTC information 0x19                                                 */
/* ------------------------------------------------------------------------- */

/*
 * Supported report types (ISO 14229-1).  A report type that has no row in
 * UDS_DcmServices[] is answered with NRC 0x12 by the DCM core.
 *
 *   0x01  reportNumberOfDTCByStatusMask
 *   0x02  reportDTCByStatusMask
 *   0x03  reportDTCSnapshotIdentification
 *   0x04  reportDTCSnapshotRecordByDTCNumber
 *   0x06  reportDTCExtDataRecordByDTCNumber
 *   0x0A  reportSupportedDTC
 *   0x14  reportDTCFaultDetectionCounter
 *
 * Response layouts (UDS_TxBuf[0] = 0x59 is filled in by the DCM core):
 *
 *   59 01 <availabilityMask> <formatId 01> <countH> <countL>
 *   59 02 <availabilityMask> [<DTC 3> <status>]...
 *   59 03 [<DTC 3> <recordNumber>]...
 *   59 04 <DTC 3> <status> [<recordNumber> <numberOfDids> [<DID 2> <data>]...]...
 *   59 06 <DTC 3> <status> [<recordNumber> <data>]...
 *   59 0A <availabilityMask> [<DTC 3> <status>]...
 *   59 14 <availabilityMask> [<DTC 3> <faultDetectionCounter>]...
 *
 * A supported DTC whose record number exists but that never failed is
 * reported without freeze frame data (0x04) or with an empty ageing counter
 * (0x06): NRC 0x31 is only used for an unknown DTC or record number.
 */
#define UDS_DCM_DTC_REPORT_NUMBER_BY_MASK   0x01U
#define UDS_DCM_DTC_REPORT_BY_MASK          0x02U
#define UDS_DCM_DTC_REPORT_SNAPSHOT_ID      0x03U
#define UDS_DCM_DTC_REPORT_SNAPSHOT_BY_DTC  0x04U
#define UDS_DCM_DTC_REPORT_EXT_DATA_BY_DTC  0x06U
#define UDS_DCM_DTC_REPORT_SUPPORTED_DTC    0x0AU
#define UDS_DCM_DTC_REPORT_FAULT_DETECTION  0x14U

/* DTCFormatIdentifier of a 0x19 01 response: ISO 14229-1 DTC format. */
#define UDS_DCM_DTC_FORMAT_ISO14229         0x01U

/*
 * Extended data records of 0x19 06 (project specific, see UDS_DemCfg.h):
 *   0x01  ageing counter                          (1 byte)
 *   0x02  ageing counter + number of occurrences  (2 bytes)
 *   0xFF  every record, each one preceded by its record number
 */
#define UDS_DCM_DTC_EXT_AGING               0x01U
#define UDS_DCM_DTC_EXT_AGING_OCCURRENCE    0x02U
#define UDS_DCM_DTC_RECORD_ALL              0xFFU

/* Append "<DTC 3>" and return the new write position. */
static unsigned int UDS_DcmDtcAppendCode(unsigned int pos, unsigned int DtcCode)
{
	UDS_TxBuf[pos] = (unsigned char)((DtcCode >> 16) & 0xFFU);
	UDS_TxBuf[pos + 1U] = (unsigned char)((DtcCode >> 8) & 0xFFU);
	UDS_TxBuf[pos + 2U] = (unsigned char)(DtcCode & 0xFFU);
	return pos + 3U;
}

/* Append "<DTC 3> <status>" and return the new write position. */
static unsigned int UDS_DcmDtcAppendCodeStatus(unsigned int pos,
                                                unsigned int DtcCode,
                                                unsigned char Status)
{
	pos = UDS_DcmDtcAppendCode(pos, DtcCode);
	UDS_TxBuf[pos] = Status;
	return pos + 1U;
}

/*
 * Append "<numberOfDids> [<DID 2> <data>]..." for the freeze frame of one DTC.
 *
 * The NVM only stores the data of the configured freeze frame DIDs (one
 * DEM_SNAPSHOT_DID_LEN block per DID), so the DID and the number of bytes to
 * report are read from the configuration table (UDS_DemCfg.c).
 */
static unsigned int UDS_DcmDtcAppendSnapshot(unsigned int pos,
                                              const DtcSnapshotData *Snapshot)
{
	unsigned int i;
	unsigned int k;
	unsigned int len;

	UDS_TxBuf[pos] = (unsigned char)UDS_DemSnapshotDidCount;
	pos++;

	for (i = 0U; (i < UDS_DemSnapshotDidCount) && (i < DEM_SNAPSHOT_DID_MAX); i++)
	{
		len = (unsigned int)UDS_DemSnapshotDids[i].Len;
		if (len > DEM_SNAPSHOT_DID_LEN)
		{
			len = DEM_SNAPSHOT_DID_LEN;
		}

		UDS_TxBuf[pos] = (unsigned char)(UDS_DemSnapshotDids[i].Did >> 8);
		pos++;
		UDS_TxBuf[pos] = (unsigned char)(UDS_DemSnapshotDids[i].Did & 0xFFU);
		pos++;

		for (k = 0U; k < len; k++)
		{
			UDS_TxBuf[pos] = Snapshot->Data[(i * DEM_SNAPSHOT_DID_LEN) + k];
			pos++;
		}
	}

	return pos;
}

/* TRUE when Need more response bytes start below the end of UDS_TxBuf[]. */
static unsigned char UDS_DcmDtcRoom(unsigned int Pos, unsigned int Need)
{
	return ((Pos + Need) <= UDS_TX_BUF_LEN) ? 1U : 0U;
}

static unsigned char UDS_DcmDtcReportNumberByMask(const unsigned char *req,
                                                   unsigned int len,
                                                   unsigned int *rspLen)
{
	const DtcEntryStruct *table;
	unsigned int count = 0U;
	unsigned int matched = 0U;
	unsigned int i;
	unsigned char avail = UDS_DtcGetStatusAvailabilityMask();
	unsigned char mask;

	/* 19 01 <statusMask> */
	if (len != 3U)
	{
		return (unsigned char)NRC_INCORRECT_MESSAGE_LENGTH_OR_INVALID_FORMAT;
	}

	table = UDS_DtcGetTable(&count);
	if (NULL_PTR == table)
	{
		return (unsigned char)NRC_CONDITIONS_NOT_CORRECT;
	}

	/* Status bits that are not supported are ignored, they can never match. */
	mask = (unsigned char)(req[2] & avail);

	for (i = 0U; i < count; i++)
	{
		if (0U != (unsigned char)(table[i].ErrSts.all & mask))
		{
			matched++;
		}
	}

	UDS_TxBuf[2] = avail;
	UDS_TxBuf[3] = UDS_DCM_DTC_FORMAT_ISO14229;
	UDS_TxBuf[4] = (unsigned char)((matched >> 8) & 0xFFU);
	UDS_TxBuf[5] = (unsigned char)(matched & 0xFFU);
	*rspLen = 6U;

	return 0U;
}

static unsigned char UDS_DcmDtcReportByMask(const unsigned char *req,
                                             unsigned int len,
                                             unsigned int *rspLen)
{
	const DtcEntryStruct *table;
	unsigned int count = 0U;
	unsigned int out;
	unsigned int i;
	unsigned char avail = UDS_DtcGetStatusAvailabilityMask();
	unsigned char mask;

	/* 19 02 <statusMask> */
	if (len != 3U)
	{
		return (unsigned char)NRC_INCORRECT_MESSAGE_LENGTH_OR_INVALID_FORMAT;
	}

	table = UDS_DtcGetTable(&count);
	if (NULL_PTR == table)
	{
		return (unsigned char)NRC_CONDITIONS_NOT_CORRECT;
	}

	mask = (unsigned char)(req[2] & avail);

	UDS_TxBuf[2] = avail;
	out = 3U;

	for (i = 0U; i < count; i++)
	{
		if (0U == (unsigned char)(table[i].ErrSts.all & mask))
		{
			continue;
		}

		if (0U == UDS_DcmDtcRoom(out, 4U))
		{
			return (unsigned char)NRC_RESPONSE_TOO_LONG;
		}

		out = UDS_DcmDtcAppendCodeStatus(out, table[i].DTC_Code, table[i].ErrSts.all);
	}

	*rspLen = out;
	return 0U;
}

static unsigned char UDS_DcmDtcReportSnapshotId(const unsigned char *req,
                                                 unsigned int len,
                                                 unsigned int *rspLen)
{
	const DtcEntryStruct *table;
	unsigned int count = 0U;
	unsigned int out;
	unsigned int i;

	(void)req;

	/* 19 03 */
	if (len != 2U)
	{
		return (unsigned char)NRC_INCORRECT_MESSAGE_LENGTH_OR_INVALID_FORMAT;
	}

	table = UDS_DtcGetTable(&count);
	if (NULL_PTR == table)
	{
		return (unsigned char)NRC_CONDITIONS_NOT_CORRECT;
	}

	out = 2U;

	for (i = 0U; i < count; i++)
	{
		if (0U == table[i].SnapshotValid)
		{
			continue;
		}

		if (0U == UDS_DcmDtcRoom(out, 4U))
		{
			return (unsigned char)NRC_RESPONSE_TOO_LONG;
		}

		out = UDS_DcmDtcAppendCode(out, table[i].DTC_Code);
		UDS_TxBuf[out] = DEM_SNAPSHOT_RECORD_NUM;
		out++;
	}

	*rspLen = out;
	return 0U;
}

static unsigned char UDS_DcmDtcReportSnapshotByDtc(const unsigned char *req,
                                                    unsigned int len,
                                                    unsigned int *rspLen)
{
	const DtcEntryStruct *dtc;
	unsigned int dtcCode;
	unsigned int out;
	unsigned char recordNum;

	/* 19 04 <DTC 3> <recordNumber> */
	if (len != 6U)
	{
		return (unsigned char)NRC_INCORRECT_MESSAGE_LENGTH_OR_INVALID_FORMAT;
	}

	dtcCode = ((unsigned int)req[2] << 16) |
	          ((unsigned int)req[3] << 8) |
	           (unsigned int)req[4];
	recordNum = req[5];

	dtc = UDS_DtcFind(dtcCode);
	if (NULL_PTR == dtc)
	{
		return (unsigned char)NRC_REQUEST_OUT_OF_RANGE;
	}

	/* One freeze frame per DTC, so 0x01 is the only record number.  0xFF
	 * addresses all records of the DTC, which is the same single record. */
	if ((DEM_SNAPSHOT_RECORD_NUM != recordNum) &&
	    (UDS_DCM_DTC_RECORD_ALL != recordNum))
	{
		return (unsigned char)NRC_REQUEST_OUT_OF_RANGE;
	}

	out = 2U;
	out = UDS_DcmDtcAppendCodeStatus(out, dtc->DTC_Code, dtc->ErrSts.all);

	/* Supported DTC without a stored freeze frame: the DTC and its status are
	 * reported, the record part is omitted (ISO 14229-1). */
	if (0U == dtc->SnapshotValid)
	{
		*rspLen = out;
		return 0U;
	}

	if (0U == UDS_DcmDtcRoom(out, 1U + (DEM_SNAPSHOT_DID_MAX * (2U + DEM_SNAPSHOT_DID_LEN))))
	{
		return (unsigned char)NRC_RESPONSE_TOO_LONG;
	}

	UDS_TxBuf[out] = DEM_SNAPSHOT_RECORD_NUM;
	out++;

	out = UDS_DcmDtcAppendSnapshot(out, &dtc->FreezeFrame);

	*rspLen = out;
	return 0U;
}

static unsigned char UDS_DcmDtcReportExtDataByDtc(const unsigned char *req,
                                                   unsigned int len,
                                                   unsigned int *rspLen)
{
	const DtcEntryStruct *dtc;
	unsigned int dtcCode;
	unsigned int out;
	unsigned char recordNum;

	/* 19 06 <DTC 3> <extendedDataRecordNumber> */
	if (len != 6U)
	{
		return (unsigned char)NRC_INCORRECT_MESSAGE_LENGTH_OR_INVALID_FORMAT;
	}

	dtcCode = ((unsigned int)req[2] << 16) |
	          ((unsigned int)req[3] << 8) |
	           (unsigned int)req[4];
	recordNum = req[5];

	dtc = UDS_DtcFind(dtcCode);
	if (NULL_PTR == dtc)
	{
		return (unsigned char)NRC_REQUEST_OUT_OF_RANGE;
	}

	if ((UDS_DCM_DTC_EXT_AGING != recordNum) &&
	    (UDS_DCM_DTC_EXT_AGING_OCCURRENCE != recordNum) &&
	    (UDS_DCM_DTC_RECORD_ALL != recordNum))
	{
		return (unsigned char)NRC_REQUEST_OUT_OF_RANGE;
	}

	out = 2U;
	out = UDS_DcmDtcAppendCodeStatus(out, dtc->DTC_Code, dtc->ErrSts.all);

	/* ISO 14229-1, Table 324 (C1): the record number and the data are only
	 * present when at least one record is available.  A DTC that was never
	 * confirmed has no extended data record, so only the DTC and its status
	 * are reported. */
	if (0U == dtc->ErrSts.bit.confirmedDTC)
	{
		*rspLen = out;
		return 0U;
	}

	/* Both records together need five bytes. */
	if (0U == UDS_DcmDtcRoom(out, 5U))
	{
		return (unsigned char)NRC_RESPONSE_TOO_LONG;
	}

	if ((UDS_DCM_DTC_EXT_AGING == recordNum) || (UDS_DCM_DTC_RECORD_ALL == recordNum))
	{
		UDS_TxBuf[out] = UDS_DCM_DTC_EXT_AGING;
		out++;
		UDS_TxBuf[out] = dtc->ExtData.AgingCnt;
		out++;
	}

	if ((UDS_DCM_DTC_EXT_AGING_OCCURRENCE == recordNum) ||
	    (UDS_DCM_DTC_RECORD_ALL == recordNum))
	{
		UDS_TxBuf[out] = UDS_DCM_DTC_EXT_AGING_OCCURRENCE;
		out++;
		UDS_TxBuf[out] = dtc->ExtData.AgingCnt;
		out++;
		UDS_TxBuf[out] = dtc->ExtData.OccurrenceCnt;
		out++;
	}

	*rspLen = out;
	return 0U;
}

static unsigned char UDS_DcmDtcReportSupported(const unsigned char *req,
                                                unsigned int len,
                                                unsigned int *rspLen)
{
	const DtcEntryStruct *table;
	unsigned int count = 0U;
	unsigned int out;
	unsigned int i;
	unsigned char avail = UDS_DtcGetStatusAvailabilityMask();

	(void)req;

	/* 19 0A */
	if (len != 2U)
	{
		return (unsigned char)NRC_INCORRECT_MESSAGE_LENGTH_OR_INVALID_FORMAT;
	}

	table = UDS_DtcGetTable(&count);
	if (NULL_PTR == table)
	{
		return (unsigned char)NRC_CONDITIONS_NOT_CORRECT;
	}

	UDS_TxBuf[2] = avail;
	out = 3U;

	for (i = 0U; i < count; i++)
	{
		if (0U == UDS_DcmDtcRoom(out, 4U))
		{
			return (unsigned char)NRC_RESPONSE_TOO_LONG;
		}

		out = UDS_DcmDtcAppendCodeStatus(out, table[i].DTC_Code, table[i].ErrSts.all);
	}

	*rspLen = out;
	return 0U;
}

static unsigned char UDS_DcmDtcReportFaultDetection(const unsigned char *req,
                                                     unsigned int len,
                                                     unsigned int *rspLen)
{
	const DtcEntryStruct *table;
	unsigned int count = 0U;
	unsigned int out;
	unsigned int i;
	unsigned char fdc;

	(void)req;

	/* 19 14 */
	if (len != 2U)
	{
		return (unsigned char)NRC_INCORRECT_MESSAGE_LENGTH_OR_INVALID_FORMAT;
	}

	table = UDS_DtcGetTable(&count);
	if (NULL_PTR == table)
	{
		return (unsigned char)NRC_CONDITIONS_NOT_CORRECT;
	}

	/* ISO 14229-1, Table 326: the response is 59 14 followed directly by
	 * DTCFaultDetectionCounterRecord[]; there is no availability mask byte,
	 * and a record is only reported for a positive counter below 0x7F (i.e.
	 * DTCs on their way to a failure, not the ones that already failed). */
	out = 2U;

	for (i = 0U; i < count; i++)
	{
		fdc = UDS_DemGetFaultDetectionCounter(table[i].DTC_Code);

		if ((0U == fdc) || (fdc >= 0x7FU))
		{
			continue;
		}

		if (0U == UDS_DcmDtcRoom(out, 4U))
		{
			return (unsigned char)NRC_RESPONSE_TOO_LONG;
		}

		out = UDS_DcmDtcAppendCode(out, table[i].DTC_Code);
		UDS_TxBuf[out] = fdc;
		out++;
	}

	*rspLen = out;
	return 0U;
}

static unsigned char UDS_DcmReadDtcInformation(const unsigned char *req,
                                                unsigned int len,
                                                unsigned int *rspLen)
{
	unsigned char reportType;
	unsigned char nrc;

	if (len < 2U)
	{
		return (unsigned char)NRC_INCORRECT_MESSAGE_LENGTH_OR_INVALID_FORMAT;
	}

	/* The DTC memory is only meaningful once the NVM store was restored. */
	if (0U == UDS_DtcIsInitialized())
	{
		return (unsigned char)NRC_CONDITIONS_NOT_CORRECT;
	}

	reportType = (unsigned char)(req[1] & 0x7FU);

	switch (reportType)
	{
		case UDS_DCM_DTC_REPORT_NUMBER_BY_MASK:
			nrc = UDS_DcmDtcReportNumberByMask(req, len, rspLen);
			break;

		case UDS_DCM_DTC_REPORT_BY_MASK:
			nrc = UDS_DcmDtcReportByMask(req, len, rspLen);
			break;

		case UDS_DCM_DTC_REPORT_SNAPSHOT_ID:
			nrc = UDS_DcmDtcReportSnapshotId(req, len, rspLen);
			break;

		case UDS_DCM_DTC_REPORT_SNAPSHOT_BY_DTC:
			nrc = UDS_DcmDtcReportSnapshotByDtc(req, len, rspLen);
			break;

		case UDS_DCM_DTC_REPORT_EXT_DATA_BY_DTC:
			nrc = UDS_DcmDtcReportExtDataByDtc(req, len, rspLen);
			break;

		case UDS_DCM_DTC_REPORT_SUPPORTED_DTC:
			nrc = UDS_DcmDtcReportSupported(req, len, rspLen);
			break;

		case UDS_DCM_DTC_REPORT_FAULT_DETECTION:
			nrc = UDS_DcmDtcReportFaultDetection(req, len, rspLen);
			break;

		default:
			nrc = (unsigned char)NRC_SUBFUNCTION_NOT_SUPPORTED;
			break;
	}

	if (0U == nrc)
	{
		/* Every 0x19 response starts with 59 <reportType>. */
		UDS_TxBuf[1] = reportType;
	}

	return nrc;
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
 *   0x14 ClearDiagnosticInformation Default, Extended     no security
 *                                   physical addressing only
 *   0x19 ReadDTCInformation         Default, Extended     no security
 *                                   mask based reports (01/02/0A) physical and
 *                                   functional, DTC specific ones (03/04/06/14)
 *                                   physical addressing only
 *   0x22 ReadDataByIdentifier       all sessions          no security
 *   0x27 SecurityAccess             all sessions          no security
 *   0x28 CommunicationControl       Extended              no security
 *   0x2E WriteDataByIdentifier      Extended, Programming Level 1
 *   0x31 RoutineControl             Extended, Programming Level 1
 *                                   physical addressing only
 *   0x85 ControlDtcSetting          Extended              no security
 *
 * The data of 0x14 / 0x19 comes from the DEM (UDS_DEM.h), the DTC memory is
 * configured in UDS_DemCfg.c.
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

	/* 0x19 ReadDTCInformation (DEM, see UDS_DEM.h). */
	{0x19U, 0x01U, (UDS_DCM_SESSION_MASK_DEFAULT | UDS_DCM_SESSION_MASK_EXTENDED), UDS_DCM_SECURITY_NONE, UDS_DCM_ADDR_MASK_ALL,      UDS_DcmReadDtcInformation},
	{0x19U, 0x02U, (UDS_DCM_SESSION_MASK_DEFAULT | UDS_DCM_SESSION_MASK_EXTENDED), UDS_DCM_SECURITY_NONE, UDS_DCM_ADDR_MASK_ALL,      UDS_DcmReadDtcInformation},
	{0x19U, 0x03U, (UDS_DCM_SESSION_MASK_DEFAULT | UDS_DCM_SESSION_MASK_EXTENDED), UDS_DCM_SECURITY_NONE, UDS_DCM_ADDR_MASK_PHYSICAL, UDS_DcmReadDtcInformation},
	{0x19U, 0x04U, (UDS_DCM_SESSION_MASK_DEFAULT | UDS_DCM_SESSION_MASK_EXTENDED), UDS_DCM_SECURITY_NONE, UDS_DCM_ADDR_MASK_PHYSICAL, UDS_DcmReadDtcInformation},
	{0x19U, 0x06U, (UDS_DCM_SESSION_MASK_DEFAULT | UDS_DCM_SESSION_MASK_EXTENDED), UDS_DCM_SECURITY_NONE, UDS_DCM_ADDR_MASK_PHYSICAL, UDS_DcmReadDtcInformation},
	{0x19U, 0x0AU, (UDS_DCM_SESSION_MASK_DEFAULT | UDS_DCM_SESSION_MASK_EXTENDED), UDS_DCM_SECURITY_NONE, UDS_DCM_ADDR_MASK_ALL,      UDS_DcmReadDtcInformation},
	{0x19U, 0x14U, (UDS_DCM_SESSION_MASK_DEFAULT | UDS_DCM_SESSION_MASK_EXTENDED), UDS_DCM_SECURITY_NONE, UDS_DCM_ADDR_MASK_PHYSICAL, UDS_DcmReadDtcInformation},

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

	/* 0x85 02 must not outlive the session either. */
	UDS_DtcSetSettingOff(0U);
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



















