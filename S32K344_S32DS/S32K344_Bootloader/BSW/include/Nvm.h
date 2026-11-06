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
 * @brief Block layout.
 *
 * Every 256-byte block is organised as
 *
 *   offset   0..3   initialisation flag (U32, must be NVM_BLOCK_MAGIC)
 *   offset   4..254 application payload (NVM_BLOCK_DATA_SIZE = 251 bytes)
 *   offset 255      checksum of bytes 0..254
 *
 * A block is valid only when the flag matches AND the checksum matches.
 * Nvm_Init() only reads: an invalid block (new device, random data, old
 * layout, corrupted content) stays invalid, reads fail and the first write
 * creates it.
 */
#define NVM_BLOCK_MAGIC         (0x55AA55AAUL)

/** Size of the initialisation flag at the start of every block. */
#define NVM_BLOCK_MAGIC_SIZE    (4U)

/** Size of the trailing checksum byte. */
#define NVM_BLOCK_CHECKSUM_SIZE (1U)

/** Offset of the first payload byte inside a block. */
#define NVM_BLOCK_DATA_OFFSET   (NVM_BLOCK_MAGIC_SIZE)

/** Number of payload bytes available to the application in one block. */
#define NVM_BLOCK_DATA_SIZE     (NVM_BLOCK_SIZE - NVM_BLOCK_MAGIC_SIZE - NVM_BLOCK_CHECKSUM_SIZE)

/** Offset of the checksum byte inside a block. */
#define NVM_BLOCK_CHECKSUM_OFFSET (NVM_BLOCK_SIZE - NVM_BLOCK_CHECKSUM_SIZE)

/**
 * @brief Maximum application payload of one block (251 bytes).
 *
 * NvmApp.h uses this value as MAX_NVM_LEN.
 */
#define NVM_BLOCK_PAYLOAD_MAX   (NVM_BLOCK_DATA_SIZE)

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
 * the RAM shadow buffer.
 *
 * For every block the 32-bit initialisation flag (NVM_BLOCK_MAGIC) is checked:
 *
 *  - flag missing (never written / random data / old layout): a brand new
 *    block is created immediately and written to DFlash; the whole payload is
 *    0xFF (erased state) so the application sees "no data" and later only has
 *    to programme the bytes that are actually used.
 *  - flag present but the checksum does not match: the corresponding bit of
 *    Nvm_GetChecksumErrorFlags() is set.  The block is NOT repaired here, the
 *    application decides what to do; the data is kept as read.
 *
 * @return E_OK on success, E_NOT_OK if the memory stack failed.
 */
Std_ReturnType Nvm_Init(void);

/**
 * @brief Returns one bit per block, set at init when the block checksum did
 *        not match the block content (bit 0 = block 0 ... bit 3 = block 3).
 *
 * The flags are only evaluated in Nvm_Init(); Nvm does not repair such a
 * block.  They are informational for the application.
 */
uint8 Nvm_GetChecksumErrorFlags(void);

/**
 * @brief Read back every block from DFlash and write it again until the
 *        DFlash content is identical to the RAM shadow.
 *
 * Intended for the "about to sleep" path: guarantees that pending changes
 * (or a failed earlier commit) are really stored before the power goes away.
 *
 * @return E_OK when all blocks match, E_NOT_OK when a block could not be
 *         written after NVM_FLUSH_RETRIES attempts.
 */
Std_ReturnType Nvm_Flush(void);

#define NVM_FLUSH_RETRIES       (3U)

/**
 * @brief Read one byte from a NVM block (from the RAM shadow).
 *
 * @param block  Block id (0..3).
 * @param offset Offset inside the block payload (0..NVM_BLOCK_DATA_SIZE-1).
 *               The 4-byte validity marker is not visible here.
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
 * @param offset Offset inside the block payload (0..NVM_BLOCK_DATA_SIZE-1).
 * @param value  Byte value to store.
 * @return E_OK on success, E_NOT_OK on invalid parameter/state.
 */
Std_ReturnType Nvm_WriteByte(Nvm_BlockIdType block, uint16 offset, uint8 value);

/**
 * @brief Get a pointer to the payload of one block in the RAM shadow.
 *
 * The returned buffer allows direct byte access, e.g.
 * <tt>Nvm_GetBlockData(NVM_BLOCK_0)[3] = 0x55;</tt>.
 * A direct write through this pointer does not mark the block dirty, so call
 * Nvm_MarkBlockDirty() (then Nvm_Commit()/Nvm_CommitBlock()) after modifying
 * the buffer.
 *
 * @param block Block id (0..3).
 * @return Pointer to the NVM_BLOCK_DATA_SIZE-byte payload, NULL_PTR on
 *         invalid id.
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
 * @brief Copy the payload of one block from the RAM shadow to a user buffer.
 *
 * @param block Block id (0..3).
 * @param data  Destination buffer, must hold at least NVM_BLOCK_DATA_SIZE bytes.
 * @return E_OK on success, E_NOT_OK on invalid parameter/state.
 */
Std_ReturnType Nvm_LoadBlock(Nvm_BlockIdType block, uint8 *data);

/**
 * @brief Copy a user buffer into the RAM shadow and persist it immediately.
 *
 * @param block Block id (0..3).
 * @param data  Source buffer, must hold at least NVM_BLOCK_DATA_SIZE bytes.
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
 * FALSE means the block was never written / the initialisation flag is
 * missing / the checksum is wrong. Reading such a block fails; the first
 * write creates it. Typical use: load factory defaults after a first boot.
 *
 * @param block Block id (0..3).
 * @return TRUE if the block was read successfully at init or written since.
 */
boolean Nvm_IsBlockValid(Nvm_BlockIdType block);

/**
 * @brief Check whether a block must be considered invalid.
 *
 * A block is invalid when it was not initialised (initialisation flag is not
 * NVM_BLOCK_MAGIC) or when its checksum does not match.  The UDS 0x22 handler
 * must call this before reading a DID and answer NRC 0x22 when it is TRUE.
 *
 * @param block Block id (0..3).
 * @return TRUE if the block is invalid (or the id is out of range).
 */
boolean Nvm_GetBlockInvalid(Nvm_BlockIdType block);

/** Per-block helpers for the UDS handlers (TRUE = block invalid). */
boolean Nvm_GetBlock0Invalid(void);
boolean Nvm_GetBlock1Invalid(void);
boolean Nvm_GetBlock2Invalid(void);
boolean Nvm_GetBlock3Invalid(void);

/**
 * @brief Mark a block invalid in the RAM shadow (no DFlash access).
 *
 * Used to roll back a failed block creation: the payload is filled with 0xFF
 * (erased state) and the next write creates the block again.
 *
 * @param block Block id (0..3).
 */
void Nvm_InvalidateBlock(Nvm_BlockIdType block);

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
