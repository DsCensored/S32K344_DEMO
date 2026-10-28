/*
 * main.c
 *
 * S32K344 UDS flash driver (RAM / ITCM image).
 *
 * This is not a C runtime startup: the image is downloaded into ITCM at 0x0
 * and the bootloader calls main(cmd, addr, len, buf) through
 * FlashDriver_Header.entry.  There is therefore no Reset_Handler/SystemInit
 * and the driver uses the bootloader stack.
 *
 * All erase/program operations go through Mem_43_INFLS, which selects the
 * correct C40 virtual sector (including the data-flash sector offset), uses
 * the core domain ID (0) and runs the C40 state machine.  The driver must run
 * from ITCM/ROM while the flash controller is busy.
 */

#include "S32K344.h"
#include "flash_driver.h"
#include "Mem_43_INFLS.h"
#include "Mem_43_INFLS_Cfg.h"
#include "MemIf_Types.h"

/* Provided by the linker script: .bss is NOLOAD and is not part of the .bin,
 * so the driver has to clear it before the first use. */
extern uint32 __bss_start[];
extern uint32 __bss_end[];

int main(uint32 cmd, uint32 addr, uint32 len, uint8 *buf);

/*----------------------------------------------------------------------------*/
/* Header at ITCM 0x0: the bootloader identifies the image and its entry.      */
/*----------------------------------------------------------------------------*/
__attribute__((section(".drv_header"), used))
const FlashDriver_HeaderType FlashDriver_Header =
{
    FLASHDRIVER_MAGIC,
    FLASHDRIVER_VERSION,
    (FlashDriver_MainFn)&main,
    0UL
};

/*----------------------------------------------------------------------------*/
/* Internal state                                                             */
/*----------------------------------------------------------------------------*/
#define FLASHDRIVER_STATE_READY        0x464C5348UL   /* "HLSF" */

static uint32 FlashDriver_State = 0UL;

/*----------------------------------------------------------------------------*/
/* Helpers                                                                    */
/*----------------------------------------------------------------------------*/
static void FlashDriver_ZeroBss(void)
{
    uint32 *p;

    for (p = __bss_start; p < __bss_end; p++)
    {
        *p = 0UL;
    }
}

static boolean FlashDriver_CheckRange(uint32 addr, uint32 len)
{
    if ((0UL == len) || (addr < FLASHDRIVER_PFLASH_START) ||
        (addr > FLASHDRIVER_PFLASH_END))
    {
        return FALSE;
    }

    /* Compare against the last usable byte to avoid a 32-bit wrap. */
    if (len > ((FLASHDRIVER_PFLASH_END - addr) + 1UL))
    {
        return FALSE;
    }

    return TRUE;
}

static sint32 FlashDriver_WaitJob(void)
{
    Mem_43_INFLS_JobResultType result;
    uint32 guard = 0UL;

    do
    {
        Mem_43_INFLS_MainFunction();
        result = Mem_43_INFLS_GetJobResult(MEM_43_INFLS_INSTANCE_0_ID);
        guard++;
    } while ((MEM_43_INFLS_JOB_PENDING == result) && (guard < 5000000UL));

    if (MEM_43_INFLS_JOB_PENDING == result)
    {
        return FLASHDRIVER_E_TIMEOUT;
    }

    if (MEM_43_INFLS_JOB_OK != result)
    {
        return FLASHDRIVER_E_FLASH;
    }

    return FLASHDRIVER_OK;
}

/*----------------------------------------------------------------------------*/
/* Commands                                                                   */
/*----------------------------------------------------------------------------*/
static sint32 FlashDriver_Init(void)
{
    /* .bss is not part of the downloaded image. */
    FlashDriver_ZeroBss();

    /* Mem_43_INFLS_Init() initialises the C40 controller as part of the
     * internal-unit initialisation. */
    Mem_43_INFLS_Init(&Mem_43_INFLS_Config);

    if (MEM_43_INFLS_JOB_FAILED ==
        Mem_43_INFLS_GetJobResult(MEM_43_INFLS_INSTANCE_0_ID))
    {
        FlashDriver_State = 0UL;
        return FLASHDRIVER_E_FLASH;
    }

    FlashDriver_State = FLASHDRIVER_STATE_READY;
    return FLASHDRIVER_OK;
}

static sint32 FlashDriver_Erase(uint32 addr, uint32 len)
{
    uint32 start;
    uint32 end;

    if (FALSE == FlashDriver_CheckRange(addr, len))
    {
        return FLASHDRIVER_E_PARAM;
    }

    /* The C40 minimum erase unit is 8 KB.  Expand a partial request to whole
     * sectors; the caller (FF00) is allowed to pass an arbitrary range. */
    start = addr & ~(FLASHDRIVER_SECTOR_SIZE - 1UL);
    end   = addr + len;

    if (0UL != (end & (FLASHDRIVER_SECTOR_SIZE - 1UL)))
    {
        end = (end + FLASHDRIVER_SECTOR_SIZE) &
              ~(FLASHDRIVER_SECTOR_SIZE - 1UL);
    }

    if (end > (FLASHDRIVER_PFLASH_END + 1UL))
    {
        return FLASHDRIVER_E_PARAM;
    }

    if (E_OK != Mem_43_INFLS_Erase(MEM_43_INFLS_INSTANCE_0_ID, start,
                                   end - start))
    {
        return FLASHDRIVER_E_FLASH;
    }

    return FlashDriver_WaitJob();
}

static sint32 FlashDriver_Write(uint32 addr, uint32 len, const uint8 *buf)
{
    if ((FALSE == FlashDriver_CheckRange(addr, len)) || (NULL_PTR == buf))
    {
        return FLASHDRIVER_E_PARAM;
    }

    /* Mem_43_INFLS programs one 128-byte page per hardware job. */
    if ((0UL != (addr & (FLASHDRIVER_WRITE_PAGE_SIZE - 1UL))) ||
        (0UL != (len  & (FLASHDRIVER_WRITE_PAGE_SIZE - 1UL))) ||
        (0UL != (((uint32)buf) & 3UL)))
    {
        return FLASHDRIVER_E_PARAM;
    }

    if (E_OK != Mem_43_INFLS_Write(MEM_43_INFLS_INSTANCE_0_ID, addr,
                                   (const Mem_43_INFLS_DataType *)buf, len))
    {
        return FLASHDRIVER_E_FLASH;
    }

    return FlashDriver_WaitJob();
}

static sint32 FlashDriver_Verify(uint32 addr, uint32 len, const uint8 *buf)
{
    uint32 i;

    if ((FALSE == FlashDriver_CheckRange(addr, len)) || (NULL_PTR == buf))
    {
        return FLASHDRIVER_E_PARAM;
    }

    for (i = 0UL; i < len; i++)
    {
        if (*(volatile uint8 *)(addr + i) != buf[i])
        {
            return FLASHDRIVER_E_VERIFY;
        }
    }

    return FLASHDRIVER_OK;
}

static sint32 FlashDriver_DeInit(void)
{
    Mem_43_INFLS_DeInit();
    FlashDriver_State = 0UL;

    return FLASHDRIVER_OK;
}

/*----------------------------------------------------------------------------*/
/* Entry point, called by the bootloader through FlashDriver_Header.entry.     */
/*----------------------------------------------------------------------------*/
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmain"
#endif

int main(uint32 cmd, uint32 addr, uint32 len, uint8 *buf)
{
    sint32 ret;

    if (FLASHDRIVER_CMD_INIT == cmd)
    {
        ret = FlashDriver_Init();
    }
    else if (FLASHDRIVER_STATE_READY != FlashDriver_State)
    {
        ret = FLASHDRIVER_E_SEQUENCE;
    }
    else
    {
        switch (cmd)
        {
            case FLASHDRIVER_CMD_ERASE:
                ret = FlashDriver_Erase(addr, len);
                break;

            case FLASHDRIVER_CMD_WRITE:
                ret = FlashDriver_Write(addr, len, buf);
                break;

            case FLASHDRIVER_CMD_VERIFY:
                ret = FlashDriver_Verify(addr, len, buf);
                break;

            case FLASHDRIVER_CMD_DEINIT:
                ret = FlashDriver_DeInit();
                break;

            default:
                ret = FLASHDRIVER_E_PARAM;
                break;
        }
    }

    return (int)ret;
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

/*----------------------------------------------------------------------------*/
/* Minimal stubs required by the RTD memory driver in this stand-alone image. */
/*----------------------------------------------------------------------------*/
Std_ReturnType Det_ReportError(uint16 ModuleId, uint8 InstanceId, uint8 ApiId,
                               uint8 ErrorId)
{
    (void)ModuleId;
    (void)InstanceId;
    (void)ApiId;
    (void)ErrorId;
    return E_OK;
}

uint8 Sys_GetCoreID(void)
{
    return 0U;
}
