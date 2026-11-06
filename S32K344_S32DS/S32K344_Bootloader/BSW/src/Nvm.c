/*
 * Nvm.c
 *
 * Simple NVM layer on top of Fee / MemAcc / Mem_43_INFLS.
 * See Nvm.h for the API description.
 */

#include <string.h>

#include "Nvm.h"
#include "Fee.h"
#include "MemAcc.h"
#include "SchM_Fee.h"
#include "SchM_MemAcc.h"

/* Timeout for a Fee/MemAcc job, in polling cycles. */
#define NVM_WAIT_CYCLES         (2000000U)

/* RAM shadow of the four NVM blocks. */
static uint8 Nvm_Shadow[NVM_BLOCK_COUNT][NVM_BLOCK_SIZE];

/* Blocks with RAM changes that are not yet written to DFlash. */
static boolean Nvm_Dirty[NVM_BLOCK_COUNT];

/* Blocks that already hold data in the DFlash (read OK or committed). */
static boolean Nvm_Valid[NVM_BLOCK_COUNT];

static Nvm_StatusType Nvm_Status = NVM_UNINIT;

/* One bit per block: set in Nvm_Init() when the block checksum did not match
 * the block content (bit 0 = block 0 ... bit 3 = block 3). */
static uint8 Nvm_ChecksumErrorFlags = 0U;

/* Create a fresh block in the RAM shadow (0xFF payload + flag + checksum). */
static void Nvm_FormatBlockRam(Nvm_BlockIdType block);

/* ------------------------------------------------------------------------- */
/* local helpers                                                             */
/* ------------------------------------------------------------------------- */

static boolean Nvm_IsValidBlock(Nvm_BlockIdType block)
{
    return ((uint32)block < (uint32)NVM_BLOCK_COUNT) ? TRUE : FALSE;
}

/**
 * @brief Check the 32-bit validity marker at the start of a block.
 *
 * The marker is handled as a little-endian U32, i.e. the same layout the
 * compiler uses for a uint32 variable.
 */
static boolean Nvm_HasValidMagic(const uint8 *block)
{
    uint32 magic = ((uint32)block[0]) |
                   ((uint32)block[1] << 8U) |
                   ((uint32)block[2] << 16U) |
                   ((uint32)block[3] << 24U);

    return (NVM_BLOCK_MAGIC == magic) ? TRUE : FALSE;
}

static void Nvm_SetMagic(uint8 *block)
{
    block[0] = (uint8)(NVM_BLOCK_MAGIC & 0xFFUL);
    block[1] = (uint8)((NVM_BLOCK_MAGIC >> 8U) & 0xFFUL);
    block[2] = (uint8)((NVM_BLOCK_MAGIC >> 16U) & 0xFFUL);
    block[3] = (uint8)((NVM_BLOCK_MAGIC >> 24U) & 0xFFUL);
}

/**
 * @brief 8-bit checksum over bytes 0..254 (flag + payload).
 *
 * The checksum byte itself (offset 255) is not included.
 */
static uint8 Nvm_CalcChecksum(const uint8 *block)
{
    uint32 sum = 0U;
    uint32 i;

    for (i = 0U; i < NVM_BLOCK_CHECKSUM_OFFSET; i++)
    {
        sum += (uint32)block[i];
    }

    return (uint8)(sum & 0xFFU);
}

static void Nvm_UpdateChecksum(uint8 *block)
{
    block[NVM_BLOCK_CHECKSUM_OFFSET] = Nvm_CalcChecksum(block);
}

static uint16 Nvm_FeeBlock(Nvm_BlockIdType block)
{
    return (uint16)(FeeConf_FeeBlockConfiguration_FeeBlockConfiguration_0 + (uint16)block);
}

/**
 * @brief Poll the Fee/MemAcc main functions until the module is idle again.
 * @return TRUE when the job finished, FALSE on timeout.
 */
static boolean Nvm_WaitIdle(void)
{
    MemIf_StatusType status;
    uint32 guard = NVM_WAIT_CYCLES;

    do
    {
        Fee_MainFunction();
        MemAcc_MainFunction();
        status = Fee_GetStatus();

        if (guard == 0U)
        {
            return FALSE;
        }
        guard--;
    } while (MEMIF_IDLE != status);

    return TRUE;
}

/**
 * @brief Write one block from the shadow to DFlash and wait for completion.
 */
static Std_ReturnType Nvm_WriteBlockToFee(Nvm_BlockIdType block)
{
    Std_ReturnType ret;

    ret = Fee_Write(Nvm_FeeBlock(block), Nvm_Shadow[block]);
    if (E_OK != ret)
    {
        return E_NOT_OK;
    }

    if ((FALSE == Nvm_WaitIdle()) || (MEMIF_JOB_OK != Fee_GetJobResult()))
    {
        return E_NOT_OK;
    }

    return E_OK;
}

/* ------------------------------------------------------------------------- */
/* public API                                                                */
/* ------------------------------------------------------------------------- */

Std_ReturnType Nvm_Init(void)
{
    uint32 i;

    Nvm_Status = NVM_BUSY;
    Nvm_ChecksumErrorFlags = 0U;

    for (i = 0U; i < NVM_BLOCK_COUNT; i++)
    {
        Nvm_Dirty[i] = FALSE;
        Nvm_Valid[i] = FALSE;
        (void)memset(Nvm_Shadow[i], 0, NVM_BLOCK_SIZE);
    }

    MemAcc_Init(NULL_PTR);
    Fee_Init(NULL_PTR);

    if ((FALSE == Nvm_WaitIdle()) || (MEMIF_JOB_OK != Fee_GetJobResult()))
    {
        Nvm_Status = NVM_JOB_FAILED;
        return E_NOT_OK;
    }

    for (i = 0U; i < NVM_BLOCK_COUNT; i++)
    {
        boolean readOk = FALSE;

        if (E_OK == Fee_Read(Nvm_FeeBlock((Nvm_BlockIdType)i), 0U,
                             Nvm_Shadow[i], NVM_BLOCK_SIZE))
        {
            if ((FALSE != Nvm_WaitIdle()) && (MEMIF_JOB_OK == Fee_GetJobResult()))
            {
                readOk = TRUE;
            }
        }

        if ((FALSE == readOk) || (FALSE == Nvm_HasValidMagic(Nvm_Shadow[i])))
        {
            /* No block yet or the initialisation flag is not in place:
             * create a brand new block, payload filled with 0xFF. */
            Nvm_FormatBlockRam((Nvm_BlockIdType)i);
            (void)Nvm_CommitBlock((Nvm_BlockIdType)i);
        }
        else
        {
            /* Flag is in place: check the checksum.  A mismatch is only
             * reported (one bit per block), the block is not repaired here;
             * the application decides what to do. */
            if (Nvm_CalcChecksum(Nvm_Shadow[i]) != Nvm_Shadow[i][NVM_BLOCK_CHECKSUM_OFFSET])
            {
                Nvm_ChecksumErrorFlags |= (uint8)(1U << i);
            }

            Nvm_Valid[i] = TRUE;
        }
    }

    Nvm_Status = NVM_IDLE;
    return E_OK;
}

uint8 Nvm_GetChecksumErrorFlags(void)
{
    return Nvm_ChecksumErrorFlags;
}

/**
 * @brief Give an invalid block a fresh header in the RAM shadow.
 *
 * The DFlash content is not touched here; the caller marks the block dirty
 * and it is written by the next commit.
 */
static void Nvm_FormatBlockRam(Nvm_BlockIdType block)
{
    /* Erased DFlash state for the payload: a later write only has to
     * programme the bytes that are actually used. */
    (void)memset(Nvm_Shadow[block], 0xFF, NVM_BLOCK_SIZE);
    Nvm_SetMagic(Nvm_Shadow[block]);
    Nvm_UpdateChecksum(Nvm_Shadow[block]);
    Nvm_Valid[block] = TRUE;
    Nvm_Dirty[block] = TRUE;
}

Std_ReturnType Nvm_ReadByte(Nvm_BlockIdType block, uint16 offset, uint8 *value)
{
    if ((FALSE == Nvm_IsValidBlock(block)) || (NULL_PTR == value) ||
        (FALSE == Nvm_Valid[block]) || (offset >= NVM_BLOCK_DATA_SIZE))
    {
        return E_NOT_OK;
    }

    *value = Nvm_Shadow[block][NVM_BLOCK_DATA_OFFSET + offset];
    return E_OK;
}

Std_ReturnType Nvm_WriteByte(Nvm_BlockIdType block, uint16 offset, uint8 value)
{
    if ((FALSE == Nvm_IsValidBlock(block)) || (offset >= NVM_BLOCK_DATA_SIZE))
    {
        return E_NOT_OK;
    }

    if (FALSE == Nvm_Valid[block])
    {
        /* First write to an invalid block: create it, the commit writes the
         * header and the payload to the DFlash. */
        Nvm_FormatBlockRam(block);
    }

    if (Nvm_Shadow[block][NVM_BLOCK_DATA_OFFSET + offset] != value)
    {
        Nvm_Shadow[block][NVM_BLOCK_DATA_OFFSET + offset] = value;
        Nvm_Dirty[block] = TRUE;
    }

    return E_OK;
}

uint8 *Nvm_GetBlockData(Nvm_BlockIdType block)
{
    if ((FALSE == Nvm_IsValidBlock(block)) || (FALSE == Nvm_Valid[block]))
    {
        return NULL_PTR;
    }

    return &Nvm_Shadow[block][NVM_BLOCK_DATA_OFFSET];
}

Std_ReturnType Nvm_MarkBlockDirty(Nvm_BlockIdType block)
{
    if ((FALSE == Nvm_IsValidBlock(block)) || (FALSE == Nvm_Valid[block]))
    {
        return E_NOT_OK;
    }

    Nvm_Dirty[block] = TRUE;
    return E_OK;
}

Std_ReturnType Nvm_LoadBlock(Nvm_BlockIdType block, uint8 *data)
{
    if ((FALSE == Nvm_IsValidBlock(block)) || (NULL_PTR == data) ||
        (FALSE == Nvm_Valid[block]))
    {
        return E_NOT_OK;
    }

    (void)memcpy(data, &Nvm_Shadow[block][NVM_BLOCK_DATA_OFFSET], NVM_BLOCK_DATA_SIZE);
    return E_OK;
}

Std_ReturnType Nvm_StoreBlock(Nvm_BlockIdType block, const uint8 *data)
{
    if ((FALSE == Nvm_IsValidBlock(block)) || (NULL_PTR == data))
    {
        return E_NOT_OK;
    }

    if (FALSE == Nvm_Valid[block])
    {
        Nvm_FormatBlockRam(block);
    }

    (void)memcpy(&Nvm_Shadow[block][NVM_BLOCK_DATA_OFFSET], data, NVM_BLOCK_DATA_SIZE);
    Nvm_SetMagic(Nvm_Shadow[block]);
    Nvm_UpdateChecksum(Nvm_Shadow[block]);
    Nvm_Dirty[block] = TRUE;

    return Nvm_CommitBlock(block);
}

Std_ReturnType Nvm_CommitBlock(Nvm_BlockIdType block)
{
    Std_ReturnType ret;

    if ((FALSE == Nvm_IsValidBlock(block)) || (FALSE == Nvm_Valid[block]))
    {
        return E_NOT_OK;
    }

    /* Refresh the initialisation flag and the checksum, then persist. */
    Nvm_SetMagic(Nvm_Shadow[block]);
    Nvm_UpdateChecksum(Nvm_Shadow[block]);

    /* Explicit call: write the block even if it is not marked dirty, so that
     * data modified through Nvm_GetBlockData() is persisted as well. */
    Nvm_Status = NVM_BUSY;
    ret = Nvm_WriteBlockToFee(block);
    if (E_OK == ret)
    {
        Nvm_Dirty[block] = FALSE;
        Nvm_Valid[block] = TRUE;
        Nvm_Status = NVM_JOB_OK;
    }
    else
    {
        Nvm_Status = NVM_JOB_FAILED;
    }

    return ret;
}

Std_ReturnType Nvm_Commit(void)
{
    Std_ReturnType ret = E_OK;
    uint32 i;

    for (i = 0U; i < NVM_BLOCK_COUNT; i++)
    {
        if (FALSE != Nvm_Dirty[i])
        {
            if (E_OK != Nvm_CommitBlock((Nvm_BlockIdType)i))
            {
                ret = E_NOT_OK;
            }
        }
    }

    return ret;
}

boolean Nvm_IsBlockDirty(Nvm_BlockIdType block)
{
    if (FALSE == Nvm_IsValidBlock(block))
    {
        return FALSE;
    }

    return Nvm_Dirty[block];
}

boolean Nvm_IsBlockValid(Nvm_BlockIdType block)
{
    if (FALSE == Nvm_IsValidBlock(block))
    {
        return FALSE;
    }

    return Nvm_Valid[block];
}

boolean Nvm_GetBlockInvalid(Nvm_BlockIdType block)
{
    if (FALSE == Nvm_IsValidBlock(block))
    {
        return TRUE;
    }

    return (FALSE == Nvm_Valid[block]) ? TRUE : FALSE;
}

boolean Nvm_GetBlock0Invalid(void)
{
    return Nvm_GetBlockInvalid(NVM_BLOCK_0);
}

boolean Nvm_GetBlock1Invalid(void)
{
    return Nvm_GetBlockInvalid(NVM_BLOCK_1);
}

boolean Nvm_GetBlock2Invalid(void)
{
    return Nvm_GetBlockInvalid(NVM_BLOCK_2);
}

boolean Nvm_GetBlock3Invalid(void)
{
    return Nvm_GetBlockInvalid(NVM_BLOCK_3);
}

void Nvm_InvalidateBlock(Nvm_BlockIdType block)
{
    if (FALSE == Nvm_IsValidBlock(block))
    {
        return;
    }

    (void)memset(Nvm_Shadow[block], 0xFF, NVM_BLOCK_SIZE);
    Nvm_Valid[block] = FALSE;
    Nvm_Dirty[block] = FALSE;
}

Std_ReturnType Nvm_Flush(void)
{
    /* Scratch buffer for the DFlash read-back (kept out of the stack). */
    static uint8 Nvm_FlashRead[NVM_BLOCK_SIZE];
    Std_ReturnType ret = E_OK;
    uint32 i;

    for (i = 0U; i < NVM_BLOCK_COUNT; i++)
    {
        boolean same = FALSE;
        uint32 attempt;

        if (FALSE == Nvm_Valid[i])
        {
            continue;
        }

        for (attempt = 0U; attempt < NVM_FLUSH_RETRIES; attempt++)
        {
            same = FALSE;

            if ((E_OK == Fee_Read(Nvm_FeeBlock((Nvm_BlockIdType)i), 0U,
                                  Nvm_FlashRead, NVM_BLOCK_SIZE)) &&
                (FALSE != Nvm_WaitIdle()) &&
                (MEMIF_JOB_OK == Fee_GetJobResult()))
            {
                same = (0 == memcmp(Nvm_FlashRead, Nvm_Shadow[i], NVM_BLOCK_SIZE)) ? TRUE : FALSE;
            }

            if (TRUE == same)
            {
                break;
            }

            /* DFlash and RAM shadow differ: write the block again. */
            (void)Nvm_CommitBlock((Nvm_BlockIdType)i);
        }

        if (FALSE == same)
        {
            ret = E_NOT_OK;
        }
    }

    return ret;
}

Nvm_StatusType Nvm_GetStatus(void)
{
    return Nvm_Status;
}

void Nvm_MainFunction(void)
{
    Fee_MainFunction();
    MemAcc_MainFunction();
}
