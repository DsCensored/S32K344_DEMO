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
 *  0xF190 (VIN) record in block 0:
 *
 *      offset  0..16  VIN (17 bytes)
 *      offset  17     magic 0x5A
 *      offset  18     magic 0xA5
 *      offset  19     checksum (bitwise complement of the byte sum of the
 *                     VIN + both magic bytes)
 *      offset  20..   unused (kept at 0x00 by the first write)
 *
 *  Nvm_F190_IsValid()/Nvm_F190_Read() reject a record whose magic or
 *  checksum is wrong, so random/leftover flash content is never returned as
 *  a VIN and never blocks the first write. Nvm_F190_Write() clears the whole
 *  block before storing the record (deterministic initialisation) and refuses
 *  to overwrite a valid VIN.
 */

#ifndef USERCODE_NVMAPP_H_
#define USERCODE_NVMAPP_H_

#include "Std_Types.h"

#define MAX_NVM_LEN 256

typedef enum{
	kBlock0_DataStart = 0,
	kBlock0_F190Data = kBlock0_DataStart,     /* F190 data, 17 bytes: 0..16 */
	kBlock0_Resv = kBlock0_F190Data + 20,     /* first free offset (VIN+magic+chk) */
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

/* 0xF190 record layout */
#define NVM_F190_VIN_LEN         (17U)
#define NVM_F190_OFFSET          (0U)
#define NVM_F190_RECORD_LEN      (NVM_F190_VIN_LEN + 3U)   /* VIN + 2 magic + checksum */

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
 * @brief Check whether block 0 contains a valid VIN record (magic + checksum).
 * @return TRUE if a valid VIN is stored, FALSE for empty/invalid data.
 */
boolean Nvm_F190_IsValid(void);

/**
 * @brief Read the VIN.
 *
 * @param Vin Destination, at least NVM_F190_VIN_LEN (17) bytes.
 * @return E_OK  - a valid record was copied,
 *         E_NOT_OK - no valid record; Vin is filled with 0x00.
 */
Std_ReturnType Nvm_F190_Read(uint8 *Vin);

/**
 * @brief Store the VIN once (write-once).
 *
 * The whole block is cleared first, so unused bytes and old garbage are
 * replaced by 0x00 and the record is written deterministically. A second
 * write is refused while a valid record exists.
 *
 * @param Vin Source, NVM_F190_VIN_LEN (17) bytes.
 * @return E_OK if the block was written, E_NOT_OK on invalid parameter,
 *         an already valid record or a memory stack failure.
 */
Std_ReturnType Nvm_F190_Write(const uint8 *Vin);

#endif /* USERCODE_NVMAPP_H_ */
