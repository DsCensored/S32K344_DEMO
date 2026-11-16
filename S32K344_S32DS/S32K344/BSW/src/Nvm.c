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

/* Write one block to the DFlash without the "module initialised" check; only
 * the start-up sequence (which creates the blocks) may use it. */
static Std_ReturnType Nvm_CommitBlockRaw(Nvm_BlockIdType block);

/* ---------------------------------------------------------------------------
 * "Module ready" interlock.
 *
 * The start-up sequence (Nvm_InitStep / Nvm_Init) runs asynchronously in the
 * application: it needs ~50 ms for the Fee scan, during which the scheduler
 * keeps running and the diagnostic stack is already alive.  Every API that
 * would touch the Fee/MemAcc stack (write, commit, flush, ...) is therefore
 * refused until the four blocks have been loaded, so a task or a diagnostic
 * request that comes too early can never drive an uninitialised memory stack.
 *
 * Reads are already safe: Nvm_Valid[] is FALSE until the block has been read,
 * so Nvm_ReadByte()/Nvm_GetBlockData() fail and NvmApp returns 0xFF.
 * ------------------------------------------------------------------------- */
static boolean Nvm_Initialised = FALSE;

/* Debug counter: number of API calls rejected because the module was not
 * initialised yet (readable with the debugger / usable as a test hook). */
volatile uint32 g_nvmNotReadyRejects = 0U;

/**
 * @brief TRUE when an operation has to be refused because the module is not
 *        initialised yet; also counts the rejection.
 */
static boolean Nvm_RejectIfNotReady(void)
{
    if (FALSE == Nvm_Initialised)
    {
        g_nvmNotReadyRejects++;
        return TRUE;
    }

    return FALSE;
}

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

/* One Nvm_InitStep() call must stay far below the 1 ms scheduler tick so the
 * 2 ms / 5 ms CAN tasks are never delayed.  The slice is bounded by the DWT
 * cycle counter of the Cortex-M7 (a core register, no driver needed) instead
 * of a fixed poll count, so the behaviour does not depend on how expensive a
 * single Fee/MemAcc poll happens to be:
 *   - measured on this project a poll costs ~50 us, i.e. two polls fit the
 *     budget and the whole Fee scan is spread over ~25 scheduler cycles;
 *   - if a poll were much cheaper, more polls are executed per call and the
 *     initialisation still finishes quickly. */
#define NVM_SLICE_US        (150U)
#define NVM_SLICE_CYCLES    (160U * NVM_SLICE_US)   /* ~160 MHz core clock */

#define NVM_REG_DEMCR       (*(volatile uint32 *)0xE000EDFCUL)
#define NVM_REG_DWT_CTRL    (*(volatile uint32 *)0xE0001000UL)
#define NVM_REG_DWT_CYCCNT  (*(volatile uint32 *)0xE0001004UL)
#define NVM_DEMCR_TRCENA    (1UL << 24U)
#define NVM_DWT_CYCCNTENA   (1UL << 0U)

/* States of the non-blocking initialisation. */
#define NVM_ST_INIT       (0U)   /* reset the module state                     */
#define NVM_ST_START      (1U)   /* MemAcc_Init + Fee_Init                     */
#define NVM_ST_SCAN       (2U)   /* wait for the Fee internal scan             */
#define NVM_ST_BLOCK_READ (3U)   /* issue the read of one block                */
#define NVM_ST_BLOCK_WAIT (4U)   /* wait for that read                         */
#define NVM_ST_BLOCK_DONE (5U)   /* evaluate one block, next one or finish     */
#define NVM_ST_DONE       (6U)   /* initialised                                */
#define NVM_ST_ERROR      (7U)   /* memory stack failure                       */

static uint8   Nvm_InitState  = NVM_ST_DONE;
static uint8   Nvm_InitBlock  = 0U;
static boolean Nvm_InitReadOk = FALSE;
static uint32  Nvm_InitPolls  = 0U;

/**
 * @brief Run Fee/MemAcc poll cycles until the job is done or the slice budget
 *        (NVM_SLICE_US) is used up.
 * @return TRUE when Fee is idle again (job finished or guard expired), FALSE
 *         when the caller has to come back for the next slice.
 */
static boolean Nvm_PollSlice(void)
{
    uint32 start;

    NVM_REG_DEMCR    |= NVM_DEMCR_TRCENA;
    NVM_REG_DWT_CTRL |= NVM_DWT_CYCCNTENA;
    start = NVM_REG_DWT_CYCCNT;

    for (;;)
    {
        Fee_MainFunction();
        MemAcc_MainFunction();
        Nvm_InitPolls++;

        if (MEMIF_IDLE == Fee_GetStatus())
        {
            return TRUE;
        }
        if (Nvm_InitPolls >= NVM_WAIT_CYCLES)
        {
            return TRUE;      /* guard expired: the caller sees the failed job */
        }
        if ((NVM_REG_DWT_CYCCNT - start) >= (uint32)NVM_SLICE_CYCLES)
        {
            return FALSE;     /* slice budget used up, yield to the scheduler */
        }
    }
}

Nvm_InitStepType Nvm_InitStep(void)
{
    switch (Nvm_InitState)
    {
    case NVM_ST_INIT:
    {
        uint32 i;

        Nvm_Status = NVM_BUSY;
        Nvm_ChecksumErrorFlags = 0U;
        Nvm_Initialised = FALSE;      /* writes are refused from here on */
        Nvm_InitPolls  = 0U;
        Nvm_InitBlock  = 0U;
        Nvm_InitReadOk = FALSE;

        for (i = 0U; i < NVM_BLOCK_COUNT; i++)
        {
            Nvm_Dirty[i] = FALSE;
            Nvm_Valid[i] = FALSE;
            (void)memset(Nvm_Shadow[i], 0, NVM_BLOCK_SIZE);
        }

        Nvm_InitState = NVM_ST_START;
        return NVM_INIT_STEP_BUSY;
    }

    case NVM_ST_START:
        MemAcc_Init(NULL_PTR);
        Fee_Init(NULL_PTR);
        Nvm_InitState = NVM_ST_SCAN;
        return NVM_INIT_STEP_BUSY;

    case NVM_ST_SCAN:
        if (FALSE == Nvm_PollSlice())
        {
            return NVM_INIT_STEP_BUSY;
        }
        if ((Nvm_InitPolls >= NVM_WAIT_CYCLES) ||
            (MEMIF_JOB_OK != Fee_GetJobResult()))
        {
            Nvm_Status = NVM_JOB_FAILED;
            Nvm_InitState = NVM_ST_ERROR;
            return NVM_INIT_STEP_ERROR;
        }
        Nvm_InitBlock = 0U;
        Nvm_InitState = NVM_ST_BLOCK_READ;
        return NVM_INIT_STEP_BUSY;

    case NVM_ST_BLOCK_READ:
        Nvm_InitReadOk = FALSE;
        if (E_OK == Fee_Read(Nvm_FeeBlock((Nvm_BlockIdType)Nvm_InitBlock), 0U,
                             Nvm_Shadow[Nvm_InitBlock], NVM_BLOCK_SIZE))
        {
            Nvm_InitState = NVM_ST_BLOCK_WAIT;
        }
        else
        {
            /* Read refused: evaluate it as "not programmed" (format below). */
            Nvm_InitState = NVM_ST_BLOCK_DONE;
        }
        return NVM_INIT_STEP_BUSY;

    case NVM_ST_BLOCK_WAIT:
        if (FALSE == Nvm_PollSlice())
        {
            return NVM_INIT_STEP_BUSY;
        }
        Nvm_InitReadOk = (MEMIF_JOB_OK == Fee_GetJobResult()) ? TRUE : FALSE;
        Nvm_InitState = NVM_ST_BLOCK_DONE;
        return NVM_INIT_STEP_BUSY;

    case NVM_ST_BLOCK_DONE:
        if ((FALSE == Nvm_InitReadOk) ||
            (FALSE == Nvm_HasValidMagic(Nvm_Shadow[Nvm_InitBlock])))
        {
            /* No block yet or the initialisation flag is not in place:
             * create a brand new block, payload filled with 0xFF.  The commit
             * is the only (rare) call that may block for a DFlash write. */
            Nvm_FormatBlockRam((Nvm_BlockIdType)Nvm_InitBlock);
            (void)Nvm_CommitBlockRaw((Nvm_BlockIdType)Nvm_InitBlock);
        }
        else
        {
            /* Flag is in place: check the checksum.  A mismatch is only
             * reported (one bit per block), the block is not repaired here;
             * the application decides what to do. */
            if (Nvm_CalcChecksum(Nvm_Shadow[Nvm_InitBlock]) !=
                Nvm_Shadow[Nvm_InitBlock][NVM_BLOCK_CHECKSUM_OFFSET])
            {
                Nvm_ChecksumErrorFlags |= (uint8)(1U << Nvm_InitBlock);
            }

            Nvm_Valid[Nvm_InitBlock] = TRUE;
        }

        Nvm_InitBlock++;
        if (Nvm_InitBlock < NVM_BLOCK_COUNT)
        {
            Nvm_InitState = NVM_ST_BLOCK_READ;
            return NVM_INIT_STEP_BUSY;
        }

        Nvm_Status = NVM_IDLE;
        Nvm_InitState = NVM_ST_DONE;
        Nvm_Initialised = TRUE;       /* the API is usable from here on */
        return NVM_INIT_STEP_DONE;

    case NVM_ST_DONE:
        return NVM_INIT_STEP_DONE;

    default:
        return NVM_INIT_STEP_ERROR;
    }
}

Std_ReturnType Nvm_Init(void)
{
    Nvm_InitStepType step;

    Nvm_InitState = NVM_ST_INIT;

    do
    {
        step = Nvm_InitStep();
    } while (NVM_INIT_STEP_BUSY == step);

    return (NVM_INIT_STEP_DONE == step) ? E_OK : E_NOT_OK;
}

uint8 Nvm_GetChecksumErrorFlags(void)
{
    return Nvm_ChecksumErrorFlags;
}

boolean Nvm_IsInitialised(void)
{
    return Nvm_Initialised;
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
    if ((FALSE == Nvm_IsValidBlock(block)) || (offset >= NVM_BLOCK_DATA_SIZE) ||
        (TRUE == Nvm_RejectIfNotReady()))
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
    if ((FALSE == Nvm_IsValidBlock(block)) || (FALSE == Nvm_Valid[block]) ||
        (TRUE == Nvm_RejectIfNotReady()))
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
    if ((FALSE == Nvm_IsValidBlock(block)) || (NULL_PTR == data) ||
        (TRUE == Nvm_RejectIfNotReady()))
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
    if (TRUE == Nvm_RejectIfNotReady())
    {
        return E_NOT_OK;
    }

    return Nvm_CommitBlockRaw(block);
}

static Std_ReturnType Nvm_CommitBlockRaw(Nvm_BlockIdType block)
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

    if (TRUE == Nvm_RejectIfNotReady())
    {
        return E_NOT_OK;
    }

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
    if ((FALSE == Nvm_IsValidBlock(block)) || (TRUE == Nvm_RejectIfNotReady()))
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

    if (TRUE == Nvm_RejectIfNotReady())
    {
        return E_NOT_OK;
    }

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
    /* Driving Fee/MemAcc before the module is initialised is meaningless and
     * would run the drivers' state machines on an unconfigured stack. */
    if (FALSE == Nvm_Initialised)
    {
        return;
    }

    Fee_MainFunction();
    MemAcc_MainFunction();
}
