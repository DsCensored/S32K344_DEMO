/*
 * Boot.c
 *
 * Bootloader boot flow, application validation/jump and the standard CRC32
 * implementation used by the programming services.
 */

#include "Boot.h"
#include "Boot_Shared.h"
#include "UserOS.h"
#include "Gpt.h"
#include "Can_43_FLEXCAN.h"
#include "UDS_DCM.h"

/* Cortex-M7 system registers, addressed directly to avoid pulling in a
 * specific CMSIS header into the bootloader application code. */
#define BOOT_SCB_VTOR       (*(volatile unsigned int *)0xE000ED08UL)
#define BOOT_NVIC_ICER(i)   (*(volatile unsigned int *)(0xE000E100UL + ((i) * 4UL)))
#define BOOT_NVIC_ICPR(i)   (*(volatile unsigned int *)(0xE000E280UL + ((i) * 4UL)))

typedef void (*Boot_AppEntryType)(void);

static unsigned char Boot_Stay = 0U;
static unsigned char Boot_Initialised = 0U;
static unsigned char Boot_ProgrammingMode = 0U;
static unsigned int Boot_EarlyMagic = 0U;
static unsigned int Boot_EarlyMagicAux = 0U;
static unsigned int Boot_StartTick = 0U;

/* The APP requested the programming session via the magic.  The bootloader
 * announces 0x50 0x02 on its own and keeps the programming session active. */
static void Boot_EnterProgramming(void)
{
    UDS_DcmCtx.Session = UDS_DCM_SESSION_PROGRAMMING;
    UDS_DcmCtx.SessionTick = BSW_GetSystemTick();

    UDS_TxBuf[0] = 0x50U;
    UDS_TxBuf[1] = 0x02U;
    (void)UDS_CanTpSend(2U);

    Boot_ProgrammingMode = 1U;
}

/* Vector table address stored in the application header. */
static unsigned int Boot_AppVector(void)
{
    return *(volatile unsigned int *)(BOOT_APP_BASE + 12UL);
}

unsigned char Boot_AppIsValid(void)
{
    unsigned int magic;
    unsigned int vector;
    unsigned int sp;
    unsigned int reset;

    magic = *(volatile unsigned int *)BOOT_APP_BASE;
    if (BOOT_APP_HEADER_MAGIC != magic)
    {
        return 0U;
    }

    vector = Boot_AppVector();
    if ((vector < (BOOT_APP_BASE + 0x2000UL)) ||
        (vector >= (BOOT_APP_BASE + BOOT_APP_SIZE)) ||
        (0U != (vector & 3UL)))
    {
        return 0U;
    }

    sp = *(volatile unsigned int *)vector;
    reset = *(volatile unsigned int *)(vector + 4UL);

    if ((sp < 0x20000000UL) || (sp > 0x20444000UL))
    {
        return 0U;
    }

    if ((reset < BOOT_APP_BASE) || (reset >= (BOOT_APP_BASE + BOOT_APP_SIZE)) ||
        (0U == (reset & 1UL)))
    {
        return 0U;
    }

    return 1U;
}

/* Called from main() before BSW_Init(): the MCAL initialisation may clear
 * the SRAM, so the APP->Boot magic must be captured first. */
void Boot_CaptureMagic(void)
{
    /* The system reset does not invalidate the D-Cache: drop the stale line
     * and disable the cache before reading, otherwise the old value hides
     * the magic written by the APP. */
    *(volatile unsigned int *)0xE000EF70UL = BOOT_RAM_FLAG_ADDR; /* DCIMVAC */
    __asm volatile ("dsb" ::: "memory");
    *(volatile unsigned int *)0xE000ED14UL &= ~(1UL << 16U);
    __asm volatile ("dsb" ::: "memory");
    __asm volatile ("isb" ::: "memory");

    /* The 16 bytes at BOOT_RAM_FLAG_ADDR are excluded from the startup ECC
     * initialisation loop.  After a power-on their ECC is uninitialised and
     * reading them raises a precise bus error, so write them once (a POR
     * cannot carry a valid magic).  A functional reset has FES != 0, keeps
     * the magic and is read normally. */
    if (0U == (*(volatile unsigned int *)0x4028C008UL & 0x20000000UL))
    {
        *(volatile unsigned int *)(BOOT_RAM_FLAG_ADDR + 0U) = 0U;
        *(volatile unsigned int *)(BOOT_RAM_FLAG_ADDR + 4U) = 0U;
        *(volatile unsigned int *)(BOOT_RAM_FLAG_ADDR + 8U) = 0U;
        *(volatile unsigned int *)(BOOT_RAM_FLAG_ADDR + 12U) = 0U;
        __asm volatile ("dsb" ::: "memory");
    }

    Boot_EarlyMagic = *(volatile unsigned int *)BOOT_RAM_FLAG_ADDR;
    Boot_EarlyMagicAux = *(volatile unsigned int *)BOOT_RAM_FLAG_AUX_ADDR;

    __asm volatile ("dsb" ::: "memory");
}

void Boot_Init(void)
{

    if (BOOT_RAM_FLAG_MAGIC == Boot_EarlyMagic)
    {
        /* The APP asked for the bootloader.  Keep the magic in place (the
         * startup code must not clear it), stay here and enter the
         * programming session, answering 0x50 0x02 without a new request. */
        Boot_Stay = 1U;
        Boot_EnterProgramming();

        /* The magic has been consumed: clear it so the next reset returns to
         * the application (the programming session stays active meanwhile). */
        Boot_RamFlagClear();
        __asm volatile ("dsb" ::: "memory");
    }
    else if (0U == Boot_AppIsValid())
    {
        /* No valid application to start. */
        Boot_Stay = 1U;
    }
    else
    {
        Boot_Stay = 0U;
    }

    Boot_StartTick = BSW_GetSystemTick();
    Boot_Initialised = 1U;
}

void Boot_StayInBoot(void)
{
    Boot_Stay = 1U;
}

static unsigned char Boot_JumpToAppRequest = 0U;
static unsigned int Boot_JumpToAppTick = 0U;

/* Called by the 10 01 handler: after the positive response is on the bus,
 * leave the bootloader and start the application. */
void Boot_RequestJumpToApp(void)
{
    Boot_JumpToAppRequest = 1U;
    Boot_JumpToAppTick = BSW_GetSystemTick();
}

/* Called from UDS_DcmCfgPoll() (DCM task context): leave the bootloader once
 * the positive response of the session change has left the CAN controller. */
void Boot_ProcessJumpRequest(void)
{
    if (0U == Boot_JumpToAppRequest)
    {
        return;
    }

    if ((BSW_GetSystemTick() - Boot_JumpToAppTick) < 50U)
    {
        return;
    }

    if (0U != UDS_CanTpIsTxBusy())
    {
        return;
    }

    Boot_JumpToAppRequest = 0U;

    if (0U != Boot_AppIsValid())
    {
        Boot_JumpToApp();
    }
}

void Boot_Poll(void)
{
    if ((0U == Boot_Initialised) || (0U != Boot_Stay))
    {
        return;
    }

    if ((BSW_GetSystemTick() - Boot_StartTick) >= BOOT_WAIT_TIMEOUT_MS)
    {
        if (0U != Boot_AppIsValid())
        {
            Boot_JumpToApp();
        }
        else
        {
            Boot_Stay = 1U;
        }
    }
}

/* Fast path used before any MCAL initialisation: with no programming request
 * and a valid application, leave the bootloader immediately (no driver init,
 * no CAN, no GPT).  Only the core state needed to start the APP is set up. */
void Boot_TryJumpToApp(void)
{
    unsigned int vector;
    unsigned int sp;
    unsigned int reset;
    unsigned int i;

    /* A programming request must keep the bootloader alive. */
    if (BOOT_RAM_FLAG_MAGIC == Boot_EarlyMagic)
    {
        return;
    }

    if (0U == Boot_AppIsValid())
    {
        return;
    }

    vector = Boot_AppVector();
    sp = *(volatile unsigned int *)vector;
    reset = *(volatile unsigned int *)(vector + 4UL);

    __asm volatile ("cpsid i" ::: "memory");

    /* No MCAL driver has been initialised: no cache maintenance needed. */

    for (i = 0U; i < 8U; i++)
    {
        BOOT_NVIC_ICER(i) = 0xFFFFFFFFUL;
        BOOT_NVIC_ICPR(i) = 0xFFFFFFFFUL;
    }

    *(volatile unsigned int *)0xE000ED94UL = 0U; /* MPU->CTRL = 0 */
    *(volatile unsigned int *)0xE000ED14UL &= ~((1UL << 17U) | (1UL << 16U));
    __asm volatile ("dsb" ::: "memory");
    __asm volatile ("isb" ::: "memory");

    *(volatile unsigned int *)0xE000ED08UL = vector; /* VTOR */
    __asm volatile ("dsb" ::: "memory");
    __asm volatile ("isb" ::: "memory");

    __asm volatile ("msr msp, %0" : : "r" (sp));
    ((Boot_AppEntryType)reset)();
}
void Boot_JumpToApp(void)
{
    unsigned int vector = Boot_AppVector();
    unsigned int sp = *(volatile unsigned int *)vector;
    unsigned int reset = *(volatile unsigned int *)(vector + 4UL);
    Boot_AppEntryType entry = (Boot_AppEntryType)reset;
    unsigned int i;

    /* De-initialise everything the bootloader owns before the APP starts. */
    __asm volatile ("cpsid i" ::: "memory");

    /* Stop the boot loader time base and clear its pending flag: a stale PIT
     * interrupt would otherwise hit the APP before it installs its own
     * PIT0 handler (default handler -> endless loop). */
    Gpt_StopTimer(GptConf_GptChannelConfiguration_GptChannelConfiguration_PIT0_CH0);
    Gpt_DeInit();
    Can_43_FLEXCAN_DeInit();

    /* Stop SysTick as well. */
    *(volatile unsigned int *)0xE000E010UL = 0U;
    *(volatile unsigned int *)0xE000E018UL = 0U;

    for (i = 0U; i < 8U; i++)
    {
        BOOT_NVIC_ICER(i) = 0xFFFFFFFFUL;
        BOOT_NVIC_ICPR(i) = 0xFFFFFFFFUL;
    }

    /* Disable MPU and caches before the jump: the APP's SystemInit() builds
     * its own MPU table, and a stale table would fault on the first fetches. */
    *(volatile unsigned int *)0xE000ED94UL = 0U;                    /* MPU->CTRL */
    *(volatile unsigned int *)0xE000ED14UL &= ~((1UL << 17U) | (1UL << 16U)); /* CCR: IC, DC */
    __asm volatile ("dsb" ::: "memory");
    __asm volatile ("isb" ::: "memory");

    BOOT_SCB_VTOR = vector;
    __asm volatile ("dsb" ::: "memory");
    __asm volatile ("isb" ::: "memory");

    /* Switch to the application stack and start its reset vector. */
    __asm volatile ("msr msp, %0" : : "r" (sp));
    entry();
}

unsigned int Boot_Crc32Update(unsigned int crc, const unsigned char *data,
                              unsigned int len)
{
    unsigned int i;
    unsigned int bit;

    if (0 == data)
    {
        return crc;
    }

    for (i = 0U; i < len; i++)
    {
        crc ^= (unsigned int)data[i];

        for (bit = 0U; bit < 8U; bit++)
        {
            if (0U != (crc & 1UL))
            {
                crc = (crc >> 1U) ^ 0xEDB88320UL;
            }
            else
            {
                crc >>= 1U;
            }
        }
    }

    return crc;
}

unsigned int Boot_Crc32(const unsigned char *data, unsigned int len)
{
    return Boot_Crc32Update(0xFFFFFFFFUL, data, len) ^ 0xFFFFFFFFUL;
}



















