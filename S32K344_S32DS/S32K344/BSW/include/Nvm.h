/*
 * Nvm.h
 *
 * Simple NVM (non-volatile memory) interface built on the AUTOSAR memory
 * stack:
 *
 *      Nvm  ->  Fee  ->  MemAcc  ->  Mem_43_INFLS  ->  C40 (internal DFlash)
 *
 * The Fee configuration provides four logical blocks of 256 bytes, which
 * live in the internal data flash (DFlash) at 0x10000000.
 * Every block is identified by one entry of @ref Nvm_BlockIdType and its
 * content can be accessed byte by byte, either through
 * Nvm_ReadByte()/Nvm_WriteByte() or through the pointer returned by
 * Nvm_GetBlockData().
 *
 * Access model:
 *  - Nvm_Init() loads all four blocks into a RAM shadow buffer (4 x 256 B)
 *  - Nvm_ReadByte()/Nvm_WriteByte()/Nvm_GetBlockData() work on the shadow
 *  - Nvm_Commit()/Nvm_CommitBlock() persist the modified blocks to DFlash
 *  - Nvm_LoadBlock()/Nvm_StoreBlock() are convenience whole-block helpers
 *
 * Nvm_WriteByte() only marks the block dirty; call Nvm_Commit() (or
 * Nvm_CommitBlock()) to make the change permanent. This keeps the original
 * 256-byte block layout and avoids erasing a DFlash sector for every byte.
 */

#ifndef NVM_H
#define NVM_H

#include "Std_Types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Size of one logical NVM block in bytes. */
#define NVM_BLOCK_SIZE          (256U)

/** Number of logical NVM blocks. */
#define NVM_BLOCK_COUNT         (4U)

/**
 * @brief Logical NVM blocks.
 *
 * Every entry addresses one 256-byte block. The numeric value is also the
 * Fee block number (1..4), so the enum can be used directly as the block
 * index for @ref Nvm_ReadByte / @ref Nvm_WriteByte.
 */
typedef enum
{
    NVM_BLOCK_0 = 0,   /**< 0: bootloader / application parameter set */
    NVM_BLOCK_1,        /**< 1: application parameter set              */
    NVM_BLOCK_2,      /**< 2: calibration data                       */
    NVM_BLOCK_3,              /**< 3: log / statistics                       */
    NVM_BLOCK_INVALID = 0xFF    /**< invalid block id                          */
} Nvm_BlockIdType;

/** Status of the NVM module. */
typedef enum
{
    NVM_UNINIT = 0,             /**< module not initialized                    */
    NVM_IDLE,                   /**< idle, no job pending                      */
    NVM_BUSY,                   /**< internal job is running                   */
    NVM_JOB_OK,                 /**< last job finished successfully            */
    NVM_JOB_FAILED              /**< last job failed                           */
} Nvm_StatusType;

/**
 * @brief Initialize the NVM module.
 *
 * Initializes MemAcc and Fee, waits until the Fee initialization (including
 * the first-time cluster format) has finished and loads the four blocks into
 * the RAM shadow buffer. Blocks that were never written are zero-filled.
 *
 * @return E_OK on success, E_NOT_OK if the memory stack failed.
 */
Std_ReturnType Nvm_Init(void);

/**
 * @brief Read one byte from a NVM block (from the RAM shadow).
 *
 * @param block  Block id (0..3).
 * @param offset Byte offset inside the block (0..255).
 * @param value  Destination of the byte.
 * @return E_OK on success, E_NOT_OK on invalid parameter/state.
 */
Std_ReturnType Nvm_ReadByte(Nvm_BlockIdType block, uint16 offset, uint8 *value);

/**
 * @brief Write one byte inside a NVM block (RAM shadow only).
 *
 * The block is marked dirty and is written to DFlash by the next
 * Nvm_Commit() / Nvm_CommitBlock() call.
 *
 * @param block  Block id (0..3).
 * @param offset Byte offset inside the block (0..255).
 * @param value  Byte value to store.
 * @return E_OK on success, E_NOT_OK on invalid parameter/state.
 */
Std_ReturnType Nvm_WriteByte(Nvm_BlockIdType block, uint16 offset, uint8 value);

/**
 * @brief Get a pointer to the whole RAM shadow of one block.
 *
 * The returned buffer allows direct byte access, e.g.
 * <tt>Nvm_GetBlockData(NVM_BLOCK_APP_PARAM)[3] = 0x55;</tt>.
 * A direct write through this pointer does not mark the block dirty, so call
 * Nvm_MarkBlockDirty() (then Nvm_Commit()/Nvm_CommitBlock()) after modifying
 * the buffer.
 *
 * @param block Block id (0..3).
 * @return Pointer to the 256-byte shadow buffer, NULL_PTR on invalid id.
 */
uint8 *Nvm_GetBlockData(Nvm_BlockIdType block);

/**
 * @brief Mark a block as modified after a direct write through
 *        Nvm_GetBlockData().
 *
 * @param block Block id (0..3).
 * @return E_OK on success, E_NOT_OK on invalid id.
 */
Std_ReturnType Nvm_MarkBlockDirty(Nvm_BlockIdType block);

/**
 * @brief Copy a whole block from the RAM shadow to a user buffer.
 *
 * @param block Block id (0..3).
 * @param data  Destination buffer, must hold at least NVM_BLOCK_SIZE bytes.
 * @return E_OK on success, E_NOT_OK on invalid parameter/state.
 */
Std_ReturnType Nvm_LoadBlock(Nvm_BlockIdType block, uint8 *data);

/**
 * @brief Copy a user buffer into the RAM shadow and persist it immediately.
 *
 * @param block Block id (0..3).
 * @param data  Source buffer, must hold at least NVM_BLOCK_SIZE bytes.
 * @return E_OK when the block was written to DFlash, E_NOT_OK otherwise.
 */
Std_ReturnType Nvm_StoreBlock(Nvm_BlockIdType block, const uint8 *data);

/**
 * @brief Persist one block to DFlash.
 *
 * The block is written even if it is not marked dirty, so this is the
 * "write this block now" call for direct Nvm_GetBlockData() modifications.
 *
 * @param block Block id (0..3).
 * @return E_OK on success, E_NOT_OK on invalid id or memory stack failure.
 */
Std_ReturnType Nvm_CommitBlock(Nvm_BlockIdType block);

/**
 * @brief Persist all blocks marked dirty to DFlash.
 *
 * @return E_OK when all dirty blocks were written, E_NOT_OK on the first
 *         failure.
 */
Std_ReturnType Nvm_Commit(void);

/**
 * @brief Check whether a block has RAM changes that are not yet persisted.
 *
 * @param block Block id (0..3).
 * @return TRUE if the block is dirty, FALSE otherwise.
 */
boolean Nvm_IsBlockDirty(Nvm_BlockIdType block);

/**
 * @brief Check whether a block already holds data in the DFlash.
 *
 * FALSE means the block was never written since the Fee area was formatted
 * (Nvm_ReadByte() then returns zeros). Typical use: load factory defaults.
 *
 * @param block Block id (0..3).
 * @return TRUE if the block was read successfully at init or committed.
 */
boolean Nvm_IsBlockValid(Nvm_BlockIdType block);

/**
 * @brief Get the module status.
 */
Nvm_StatusType Nvm_GetStatus(void);

/**
 * @brief Periodic main function.
 *
 * Calls Fee_MainFunction() and MemAcc_MainFunction(). The API above already
 * polls them internally, so this function is only needed if the application
 * runs the memory stack asynchronously.
 */
void Nvm_MainFunction(void);

#ifdef __cplusplus
}
#endif

#endif /* NVM_H */
