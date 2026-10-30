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

/* ------------------------------------------------------------------------- */
/* local helpers                                                             */
/* ------------------------------------------------------------------------- */

static boolean Nvm_IsValidBlock(Nvm_BlockIdType block)
{
    return ((uint32)block < (uint32)NVM_BLOCK_COUNT) ? TRUE : FALSE;
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
        if (E_OK == Fee_Read(Nvm_FeeBlock((Nvm_BlockIdType)i), 0U, Nvm_Shadow[i], NVM_BLOCK_SIZE))
        {
            if (FALSE == Nvm_WaitIdle())
            {
                /* Timeout: keep the zero-filled shadow, block is invalid. */
                (void)memset(Nvm_Shadow[i], 0, NVM_BLOCK_SIZE);
            }
            else if (MEMIF_JOB_OK == Fee_GetJobResult())
            {
                Nvm_Valid[i] = TRUE;
            }
            else
            {
                /* Block was never written: keep the zero-filled shadow. */
                (void)memset(Nvm_Shadow[i], 0, NVM_BLOCK_SIZE);
            }
        }
        else
        {
            /* Empty block, keep the zero-filled shadow. */
        }
    }

    Nvm_Status = NVM_IDLE;
    return E_OK;
}

Std_ReturnType Nvm_ReadByte(Nvm_BlockIdType block, uint16 offset, uint8 *value)
{
    if ((FALSE == Nvm_IsValidBlock(block)) || (NULL_PTR == value) || (offset >= NVM_BLOCK_SIZE))
    {
        return E_NOT_OK;
    }

    *value = Nvm_Shadow[block][offset];
    return E_OK;
}

Std_ReturnType Nvm_WriteByte(Nvm_BlockIdType block, uint16 offset, uint8 value)
{
    if ((FALSE == Nvm_IsValidBlock(block)) || (offset >= NVM_BLOCK_SIZE))
    {
        return E_NOT_OK;
    }

    if (Nvm_Shadow[block][offset] != value)
    {
        Nvm_Shadow[block][offset] = value;
        Nvm_Dirty[block] = TRUE;
    }

    return E_OK;
}

uint8 *Nvm_GetBlockData(Nvm_BlockIdType block)
{
    if (FALSE == Nvm_IsValidBlock(block))
    {
        return NULL_PTR;
    }

    return &Nvm_Shadow[block][0];
}

Std_ReturnType Nvm_MarkBlockDirty(Nvm_BlockIdType block)
{
    if (FALSE == Nvm_IsValidBlock(block))
    {
        return E_NOT_OK;
    }

    Nvm_Dirty[block] = TRUE;
    return E_OK;
}

Std_ReturnType Nvm_LoadBlock(Nvm_BlockIdType block, uint8 *data)
{
    if ((FALSE == Nvm_IsValidBlock(block)) || (NULL_PTR == data))
    {
        return E_NOT_OK;
    }

    (void)memcpy(data, Nvm_Shadow[block], NVM_BLOCK_SIZE);
    return E_OK;
}

Std_ReturnType Nvm_StoreBlock(Nvm_BlockIdType block, const uint8 *data)
{
    if ((FALSE == Nvm_IsValidBlock(block)) || (NULL_PTR == data))
    {
        return E_NOT_OK;
    }

    (void)memcpy(Nvm_Shadow[block], data, NVM_BLOCK_SIZE);
    Nvm_Dirty[block] = TRUE;

    return Nvm_CommitBlock(block);
}

Std_ReturnType Nvm_CommitBlock(Nvm_BlockIdType block)
{
    Std_ReturnType ret;

    if (FALSE == Nvm_IsValidBlock(block))
    {
        return E_NOT_OK;
    }

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

Nvm_StatusType Nvm_GetStatus(void)
{
    return Nvm_Status;
}

void Nvm_MainFunction(void)
{
    Fee_MainFunction();
    MemAcc_MainFunction();
}
