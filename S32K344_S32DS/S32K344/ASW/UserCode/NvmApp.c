/*
 * NvmApp.c
 *
 *  Created on: 2026年10月29日
 *      Author: CENSORED
 *
 *  Byte oriented access to the four NVM blocks. The data itself is kept in
 *  the RAM shadow of the Nvm module (loaded once by Nvm_Init), so reading is
 *  a plain memory access and writing only marks the block for a later commit.
 *
 *  The 0xF190 record is protected by two magic bytes and a checksum, see
 *  NvmApp.h. This makes random / half erased flash content harmless: it is
 *  detected as invalid, read as "not programmed" and can be initialised by
 *  the first write.
 */

#include <string.h>

#include "NvmApp.h"
#include "Nvm.h"

/* Block waiting for a DFlash commit; NVM_BLOCK_INVALID = no pending write. */
static Nvm_BlockIdType Nvm_AppWriteReq = NVM_BLOCK_INVALID;

/* Scratch buffer for the 0xF190 record (whole 256-byte block). */
static uint8 Nvm_F190Block[MAX_NVM_LEN];

/* 0xF190 record protection */
#define NVM_F190_MAGIC0          (0x5AU)
#define NVM_F190_MAGIC1          (0xA5U)
#define NVM_F190_MAGIC_OFFSET    (NVM_F190_OFFSET + NVM_F190_VIN_LEN)
#define NVM_F190_CHECK_OFFSET    (NVM_F190_MAGIC_OFFSET + 2U)

static uint8 Nvm_AppRead(Nvm_BlockIdType Block, uint16 Offset)
{
	uint8 value = 0U;

	(void)Nvm_ReadByte(Block, Offset, &value);
	return value;
}

static Std_ReturnType Nvm_AppWrite(Nvm_BlockIdType Block, uint16 Offset, uint8 Data)
{
	Std_ReturnType ret;

	/* Only one block may wait for a commit: the pending request of another
	 * block would be lost otherwise. */
	if ((NVM_BLOCK_INVALID != Nvm_AppWriteReq) && (Nvm_AppWriteReq != Block))
	{
		return E_NOT_OK;
	}

	ret = Nvm_WriteByte(Block, Offset, Data);
	if ((E_OK == ret) && (TRUE == Nvm_IsBlockDirty(Block)))
	{
		Nvm_AppWriteReq = Block;
	}

	return ret;
}

/**
 * @brief Checksum of the VIN record: complement of the byte sum.
 */
static uint8 Nvm_F190Checksum(const uint8 *Vin)
{
	uint16 sum = (uint16)NVM_F190_MAGIC0 + (uint16)NVM_F190_MAGIC1;
	uint16 i;

	for (i = 0U; i < NVM_F190_VIN_LEN; i++)
	{
		sum = (uint16)(sum + (uint16)Vin[i]);
	}

	return (uint8)((uint8)(sum & 0xFFU) ^ 0xFFU);
}

boolean Nvm_F190_IsValid(void)
{
	const uint8 *block = Nvm_GetBlockData(NVM_BLOCK_0);

	if (NULL_PTR == block)
	{
		return FALSE;
	}

	if ((NVM_F190_MAGIC0 != block[NVM_F190_MAGIC_OFFSET]) ||
	    (NVM_F190_MAGIC1 != block[NVM_F190_MAGIC_OFFSET + 1U]))
	{
		return FALSE;
	}

	if (Nvm_F190Checksum(&block[NVM_F190_OFFSET]) != block[NVM_F190_CHECK_OFFSET])
	{
		return FALSE;
	}

	return TRUE;
}

Std_ReturnType Nvm_F190_Read(uint8 *Vin)
{
	if (NULL_PTR == Vin)
	{
		return E_NOT_OK;
	}

	if (FALSE == Nvm_F190_IsValid())
	{
		(void)memset(Vin, 0, NVM_F190_VIN_LEN);
		return E_NOT_OK;
	}

	(void)memcpy(Vin, Nvm_GetBlockData(NVM_BLOCK_0) + NVM_F190_OFFSET, NVM_F190_VIN_LEN);
	return E_OK;
}

Std_ReturnType Nvm_F190_Write(const uint8 *Vin)
{
	if (NULL_PTR == Vin)
	{
		return E_NOT_OK;
	}

	/* Write once: a valid VIN must not be overwritten. */
	if (TRUE == Nvm_F190_IsValid())
	{
		return E_NOT_OK;
	}

	/* Deterministic initialisation: clear the complete block, then store the
	 * record. Random leftovers outside the record are removed by this. */
	(void)memset(Nvm_F190Block, 0, MAX_NVM_LEN);
	(void)memcpy(&Nvm_F190Block[NVM_F190_OFFSET], Vin, NVM_F190_VIN_LEN);
	Nvm_F190Block[NVM_F190_MAGIC_OFFSET] = NVM_F190_MAGIC0;
	Nvm_F190Block[NVM_F190_MAGIC_OFFSET + 1U] = NVM_F190_MAGIC1;
	Nvm_F190Block[NVM_F190_CHECK_OFFSET] = Nvm_F190Checksum(Vin);

	/* Copy into the Nvm shadow and commit the block immediately. */
	if (E_OK != Nvm_StoreBlock(NVM_BLOCK_0, Nvm_F190Block))
	{
		/* Commit failed: remove the validity marker from the RAM shadow so
		 * the VIN is reported as "not programmed" and a retry is possible. */
		(void)Nvm_WriteByte(NVM_BLOCK_0, (uint16)NVM_F190_MAGIC_OFFSET, 0x00U);
		(void)Nvm_WriteByte(NVM_BLOCK_0, (uint16)(NVM_F190_MAGIC_OFFSET + 1U), 0x00U);
		return E_NOT_OK;
	}

	return E_OK;
}

uint8 Nvm_Block0_Read_U8(Block0Data Aim)
{
	return Nvm_AppRead(NVM_BLOCK_0, (uint16)Aim);
}

uint8 Nvm_Block1_Read_U8(Block1Data Aim)
{
	return Nvm_AppRead(NVM_BLOCK_1, (uint16)Aim);
}

uint8 Nvm_Block2_Read_U8(Block2Data Aim)
{
	return Nvm_AppRead(NVM_BLOCK_2, (uint16)Aim);
}

uint8 Nvm_Block3_Read_U8(Block3Data Aim)
{
	return Nvm_AppRead(NVM_BLOCK_3, (uint16)Aim);
}

Std_ReturnType Nvm_Block0_Write_U8(Block0Data Aim, uint8 Data)
{
	return Nvm_AppWrite(NVM_BLOCK_0, (uint16)Aim, Data);
}

Std_ReturnType Nvm_Block1_Write_U8(Block1Data Aim, uint8 Data)
{
	return Nvm_AppWrite(NVM_BLOCK_1, (uint16)Aim, Data);
}

Std_ReturnType Nvm_Block2_Write_U8(Block2Data Aim, uint8 Data)
{
	return Nvm_AppWrite(NVM_BLOCK_2, (uint16)Aim, Data);
}

Std_ReturnType Nvm_Block3_Write_U8(Block3Data Aim, uint8 Data)
{
	return Nvm_AppWrite(NVM_BLOCK_3, (uint16)Aim, Data);
}

Std_ReturnType Nvm_UserBlock_Polling_10ms(void)
{
	Std_ReturnType ret = E_OK;

	if (NVM_BLOCK_INVALID != Nvm_AppWriteReq)
	{
		ret = Nvm_CommitBlock(Nvm_AppWriteReq);
		Nvm_AppWriteReq = NVM_BLOCK_INVALID;
	}

	return ret;
}
