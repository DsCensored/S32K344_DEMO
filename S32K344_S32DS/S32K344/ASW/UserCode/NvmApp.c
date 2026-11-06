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
 *  DIDs do not add their own magic/checksum: block validity is covered by the
 *  block initialisation flag and the block checksum (see Nvm.h).
 */

#include <string.h>

#include "NvmApp.h"
#include "Nvm.h"

/* Block waiting for a DFlash commit; NVM_BLOCK_INVALID = no pending write. */
static Nvm_BlockIdType Nvm_AppWriteReq = NVM_BLOCK_INVALID;

static uint8 Nvm_AppRead(Nvm_BlockIdType Block, uint16 Offset)
{
	uint8 value = NVM_UNUSED_BYTE;

	/* Nothing stored / not readable: behave like erased DFlash. */
	if (E_OK != Nvm_ReadByte(Block, Offset, &value))
	{
		value = NVM_UNUSED_BYTE;
	}

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

Std_ReturnType Nvm_F190_Read(uint8 *Vin)
{
	if (NULL_PTR == Vin)
	{
		return E_NOT_OK;
	}

	/* No block check here: an unwritten F190 simply reads as 0xFF. */
	for(uint8 i = 0; i < NVM_F190_VIN_LEN; i++)
	{
		Vin[i] = Nvm_Block0_Read_U8(kBlock0_F190Data + i);
	}

	return E_OK;
}

Std_ReturnType Nvm_F190_Write(const uint8 *Vin)
{
	uint8 i;

	if (NULL_PTR == Vin)
	{
		return E_NOT_OK;
	}

	/* Write once: refuse when any F190 byte was already programmed
	 * (something else than 0xFF is stored). */
	for (i = 0U; i < NVM_F190_VIN_LEN; i++)
	{
		if (NVM_UNUSED_BYTE != Nvm_Block0_Read_U8(kBlock0_F190Data + i))
		{
			return E_NOT_OK;
		}
	}

	/* Only the 17 F190 bytes are touched, the rest of the block keeps its
	 * content (other DIDs may live in the same block). */
	for (i = 0U; i < NVM_F190_VIN_LEN; i++)
	{
		if (E_OK != Nvm_Block0_Write_U8(kBlock0_F190Data + i, Vin[i]))
		{
			return E_NOT_OK;
		}
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
