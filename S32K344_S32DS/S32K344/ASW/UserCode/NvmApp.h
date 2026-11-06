/*
 * NvmApp.h
 *
 *  Created on: 2026年10月29日
 *      Author: CENSORED
 *
 *  Application view of the four NVM blocks: one enum per block so a byte can
 *  be addressed by name. C puts all enumerators into one namespace, therefore
 *  the names are prefixed with the block number (kBlock0_... etc.).
 *
 *  The wrapper functions work on the RAM shadow of the Nvm module:
 *    - Nvm_BlockX_Read_U8()  returns the current byte (fast, no DFlash access)
 *    - Nvm_BlockX_Write_U8() updates the shadow and requests a commit
 *    - Nvm_UserBlock_Polling_10ms() writes the pending block to the DFlash
 *      (call it from a cyclic task; a UDS write may also call it directly to
 *      make the value permanent before answering)
 *
 *  DIDs do not carry their own magic/checksum any more: the validity of every
 *  block is covered by the block initialisation flag (0x55AA55AA) and the
 *  block checksum, see Nvm.h.  0xF190 is simply the first 17 payload bytes of
 *  block 0; the rest of the block is 0xFF (erased) so that a later write only
 *  has to programme the used bytes.
 */

#ifndef USERCODE_NVMAPP_H_
#define USERCODE_NVMAPP_H_

#include "Std_Types.h"
#include "Nvm.h"

/*
 * Maximum payload of one NVM block.  The block header (4-byte initialisation
 * flag + 1-byte checksum) is not part of it:
 *     256 - 4 - 1 = 251 bytes
 * The BlockXData enums below start at 0 for the payload; the Nvm module adds
 * NVM_BLOCK_DATA_OFFSET (=4) when it accesses the DFlash block.
 */
#define MAX_NVM_LEN NVM_BLOCK_DATA_SIZE

/** Value of a payload byte that was never written (erased DFlash). */
#define NVM_UNUSED_BYTE          (0xFFU)

typedef enum{
	kBlock0_DataStart = 0,
	kBlock0_F190Data = kBlock0_DataStart,     /* F190 data, 17 bytes: 0..16 */
	kBlock0_Resv = kBlock0_F190Data + 17,     /* first free offset */
	kBlock0_End = MAX_NVM_LEN - 1
}Block0Data;

typedef enum{
	kBlock1_DataStart = 0,
	kBlock1_Resv = kBlock1_DataStart,
	kBlock1_End = MAX_NVM_LEN - 1
}Block1Data;

typedef enum{
	kBlock2_DataStart = 0,
	kBlock2_Resv = kBlock2_DataStart,
	kBlock2_End = MAX_NVM_LEN - 1
}Block2Data;

typedef enum{
	kBlock3_DataStart = 0,
	kBlock3_Resv = kBlock3_DataStart,
	kBlock3_End = MAX_NVM_LEN - 1
}Block3Data;

/* 0xF190 DID: payload offsets inside block 0 */
#define NVM_F190_VIN_LEN         (17U)
#define NVM_F190_OFFSET          (0U)

uint8 Nvm_Block0_Read_U8(Block0Data Aim);
uint8 Nvm_Block1_Read_U8(Block1Data Aim);
uint8 Nvm_Block2_Read_U8(Block2Data Aim);
uint8 Nvm_Block3_Read_U8(Block3Data Aim);

Std_ReturnType Nvm_Block0_Write_U8(Block0Data Aim, uint8 Data);
Std_ReturnType Nvm_Block1_Write_U8(Block1Data Aim, uint8 Data);
Std_ReturnType Nvm_Block2_Write_U8(Block2Data Aim, uint8 Data);
Std_ReturnType Nvm_Block3_Write_U8(Block3Data Aim, uint8 Data);

/**
 * @brief Commit the block that is waiting for a DFlash write.
 *
 * @return E_OK when nothing was pending or the commit succeeded,
 *         E_NOT_OK when the Fee/MemAcc job failed.
 */
Std_ReturnType Nvm_UserBlock_Polling_10ms(void);


/**
 * @brief Read the VIN (first 17 payload bytes of block 0).
 *
 * No block validity check is done here: a block that was never programmed
 * simply reads as 0xFF in every byte.
 *
 * @param Vin Destination, at least NVM_F190_VIN_LEN (17) bytes.
 * @return E_OK when the bytes were copied, E_NOT_OK on a NULL pointer.
 */
Std_ReturnType Nvm_F190_Read(uint8 *Vin);

/**
 * @brief Store the VIN once (write-once, byte wise).
 *
 * The write is refused when any of the 17 F190 bytes is not 0xFF, i.e. when a
 * VIN was already programmed.  Only these 17 bytes are modified, the rest of
 * the block keeps its content (other DIDs may share block 0).
 *
 * @param Vin Source, NVM_F190_VIN_LEN (17) bytes.
 * @return E_OK if the bytes were written, E_NOT_OK on a NULL pointer or when
 *         F190 is already programmed.
 */
Std_ReturnType Nvm_F190_Write(const uint8 *Vin);

#endif /* USERCODE_NVMAPP_H_ */
