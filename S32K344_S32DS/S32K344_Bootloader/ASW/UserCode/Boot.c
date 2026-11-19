/*
 * Boot.c
 *
 * Bootloader boot flow and application validation/jump.
 *
 * The integrity checks of the programming services (former software CRC32)
 * are now done by the HSE SHA-256 engine; see Boot_Program.c and
 * hse_hash.c.  The digest of the active image is stored in NVM block 3 by
 * 0xFF02 and verified again at every start (see Boot_AppShaVerify()).
 *
 * With the HSE AB swap firmware the active and passive partitions are swapped
 * by the HSE at reset; the active area (0x00400000 / 0x00448000) is the
 * address the bootloader and the APP always run from, so the start-up check
 * below does not have to know which physical partition is active.
 */

#include <string.h>

#include "Boot.h"
#include "Boot_Shared.h"
#include "Boot_Flash.h"
#include "Boot_Program.h"
#include "UserOS.h"
#include "Gpt.h"
#include "Can_43_FLEXCAN.h"
#include "UDS_DCM.h"
#include "CDD_Mcl.h"
#include "MemAcc.h"
#include "Mem_43_INFLS.h"
#include "Nvm.h"
#include "NvmApp.h"      /* Block3Data: kBlock3_Now_SHAdata / _FileSize */
#include "hse_hash.h"    /* HSE SHA-256 of the APP image                 */
#include "BSW_Init.h"    /* BSW_EarlyInitClock() / BSW_EarlyInitNvm()     */

/* Cortex-M7 system registers, addressed directly to avoid pulling in a
 * specific CMSIS header into the bootloader application code. */
#define BOOT_SCB_VTOR       (*(volatile unsigned int *)0xE000ED08UL)
#define BOOT_NVIC_ICER(i)   (*(volatile unsigned int *)(0xE000E100UL + ((i) * 4UL)))
#define BOOT_NVIC_ICPR(i)   (*(volatile unsigned int *)(0xE000E280UL + ((i) * 4UL)))

typedef void (*Boot_AppEntryType)(void);

/* Start the application: switch to its stack and branch to its reset vector.
 *
 * Must be naked.  The MSP is switched to the APP stack top (0x20020000) here,
 * so the compiler must not emit any epilogue (ldmia sp!, ... / pop) after it:
 * reading past the end of the DTCM stack raises a precise bus fault. */
static void __attribute__((naked, noreturn)) Boot_StartApp(unsigned int stackPointer,
                                                           unsigned int resetHandler)
{
    __asm volatile ("msr msp, r0\n"
                    "dsb\n"
                    "isb\n"
                    "bx  r1\n");
}

static unsigned char Boot_Stay = 0U;
static unsigned char Boot_Initialised = 0U;
static unsigned char Boot_ProgrammingMode = 0U;
static unsigned int Boot_EarlyMagic = 0U;
static unsigned int Boot_EarlyMagicAux = 0U;
static unsigned int Boot_EarlyAbSwap = 0U;
static unsigned int Boot_StartTick = 0U;

/* The APP requested the programming session via the magic.  The bootloader
 * announces 0x50 0x02 on its own and keeps the programming session active. */
static void Boot_EnterProgramming(void)
{
    /* A previous session may have left a driver image in ITCM. */
    BootProg_DestroyFlash();

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

/* ---------------------------------------------------------------------------
 * APP image integrity check (SHA-256 + length stored in NVM block 3)
 *
 * 0xFF02 stores the SHA-256 computed by 0x37 and the length of the image that
 * is going to become active in NVM block 3 (kBlock3_Now_SHAdata /
 * kBlock3_Now_FileSize).  At every start the active area is hashed again with
 * the HSE and compared with those values; only a match allows the APP to be
 * started.
 *
 * Result state.  "Bad" is sticky for the whole power cycle, so Boot_Init(),
 * Boot_Poll() and the 10 01 handler (which all go through Boot_AppIsValid())
 * cannot start an image that failed the check.
 * ------------------------------------------------------------------------- */

#define BOOT_SHA_STATE_UNKNOWN  0U   /* not checked in this power cycle       */
#define BOOT_SHA_STATE_OK       1U   /* run area matches the NVM values       */
#define BOOT_SHA_STATE_BAD      2U   /* mismatch / no value: never start      */

/* Failure reasons of g_bootAppShaDiag[2]. */
#define BOOT_SHA_ERR_BLOCK      0x100U   /* NVM block 3 invalid / unreadable  */
#define BOOT_SHA_ERR_SIZE       0x101U   /* stored length 0 or > APP size     */
#define BOOT_SHA_ERR_HSE_HASH   0x102U   /* HSE SHA-256 failed                */
#define BOOT_SHA_ERR_MISMATCH   0x103U   /* digest differs from the NVM value */
#define BOOT_SHA_ERR_NVM_INIT   0x104U   /* Nvm_Init() failed                 */
#define BOOT_SHA_ERR_HSE_INIT   0x105U   /* HSE not ready                     */

static unsigned char Boot_AppShaState = BOOT_SHA_STATE_UNKNOWN;

/* Debug view for the J-Link (the two digests are kept in the stack only while
 * the check runs, so they are not kept in RAM afterwards):
 *   [0] state (0/1/2)         [1] length stored in NVM
 *   [2] failure reason        [3] HseHash_GetLastError()
 *   [4] 0x53484132 ("SHA2" marker) */
volatile unsigned int g_bootAppShaDiag[5] = {
    0xFFFFFFFFUL, 0U, 0U, 0U, 0x53484132UL
};

/* Reset-time delay: no timer is running on the fast path. */
static void Boot_DelayLoop(unsigned int loops)
{
    volatile unsigned int i;

    for (i = 0U; i < loops; i++)
    {
        /* wait */
    }
}

/* ---------------------------------------------------------------------------
 * Phase timing of the start-up check.
 *
 * The DWT cycle counter of the Cortex-M7 needs no driver (PPB registers), so
 * the duration of every phase can be read with the debugger even on the fast
 * path.  g_bootAppShaTime[] holds core cycles; after the PLL is up (160 MHz)
 * 160 cycles = 1 us (the clock initialisation itself is counted at the reset
 * clock, i.e. ~48 MHz).
 *   [0] clock init   [1] HSE probe   [2] NVM init    [3] Block3 read
 *   [4] HSE wait     [5] hash+compare               [6] total
 *   [7] 0x54494D45 ("TIME" marker)
 * ------------------------------------------------------------------------- */

#define BOOT_DEMCR          (*(volatile unsigned int *)0xE000EDFCUL)
#define BOOT_DWT_CTRL       (*(volatile unsigned int *)0xE0001000UL)
#define BOOT_DWT_CYCCNT     (*(volatile unsigned int *)0xE0001004UL)
#define BOOT_DEMCR_TRCENA   (1UL << 24U)
#define BOOT_DWT_CYCCNTENA  (1UL << 0U)

volatile unsigned int g_bootAppShaTime[8] = {
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0x54494D45UL
};

static void Boot_TimeInit(void)
{
    BOOT_DEMCR      |= BOOT_DEMCR_TRCENA;
    BOOT_DWT_CYCCNT  = 0U;
    BOOT_DWT_CTRL   |= BOOT_DWT_CYCCNTENA;
    __asm volatile ("dsb" ::: "memory");
}

static unsigned int Boot_TimeGet(void)
{
    return BOOT_DWT_CYCCNT;
}

/* Non-blocking HSE readiness probe: one MU status register read.
 * Returns 1 when the HSE already answers, 0 when it is still booting. */
static unsigned char Boot_HseProbe(void)
{
    return (HSE_HASH_OK == HseHash_Init()) ? 1U : 0U;
}

/* Wait for the HSE, only used when Boot_HseProbe() failed before the NVM
 * initialisation.  The budget covers the *remaining* part of the HSE boot
 * (the HSE boots in parallel with the M7, and the Fee/DFlash start-up below
 * already gives it ~10 ms), so this loop normally runs zero times. */
static unsigned char Boot_HseWaitReady(void)
{
    unsigned int retry;

    for (retry = 0U; retry < BOOT_HSE_INIT_RETRIES; retry++)
    {
        if (0U != Boot_HseProbe())
        {
            return 1U;
        }

        Boot_DelayLoop(BOOT_HSE_INIT_DELAY);
    }

    return 0U;
}

/* Little-endian 32-bit value from NVM block 3 (see BootProg_StoreImageInfo). */
static unsigned int Boot_NvmReadU32(unsigned int offset)
{
    unsigned int i;
    unsigned int value = 0U;

    for (i = 0U; i < 4U; i++)
    {
        value |= ((unsigned int)Nvm_Block3_Read_U8((Block3Data)(offset + i)))
                 << (8U * i);
    }

    return value;
}

/* Hash the run area and compare it with the values stored by 0xFF02.
 * @return 1 = the APP may be started, 0 = stay in the bootloader. */
static unsigned char Boot_AppShaVerify(void)
{
    unsigned char stored[HSE_HASH_SHA256_SIZE];
    unsigned char digest[HSE_HASH_SHA256_SIZE];
    unsigned char hseReady;
    unsigned int last;
    unsigned int now;
    unsigned int size;
    unsigned int i;

    /* Fail closed: only the path below that proves integrity opens the APP. */
    Boot_AppShaState    = BOOT_SHA_STATE_BAD;
    g_bootAppShaDiag[0] = (unsigned int)BOOT_SHA_STATE_BAD;
    g_bootAppShaDiag[2] = 0U;
    g_bootAppShaDiag[3] = 0U;

    /* Phase timing for the debugger (see g_bootAppShaTime[]). */
    Boot_TimeInit();
    last = Boot_TimeGet();

    /* No MCAL driver is initialised when the fast path runs: the NVM stack
     * needs the DFlash driver and the HSE its clock.  Both helpers mirror the
     * BSW_Init() bring-up and are idempotent, so the normal boot continues
     * with whatever is already initialised. */
    (void)BSW_EarlyInitClock();

    now = Boot_TimeGet();
    g_bootAppShaTime[0] = now - last;
    last = now;

    /* The HSE firmware boots in parallel with the M7 after a power-on.  Probe
     * it here (one MU status read, no waiting) and let the NVM start-up below
     * serve as its boot time instead of idling in a delay loop: the wait only
     * runs for whatever is left after the Fee/DFlash scan. */
    hseReady = Boot_HseProbe();

    now = Boot_TimeGet();
    g_bootAppShaTime[1] = now - last;
    last = now;

    if (E_OK != BSW_EarlyInitNvm())
    {
        g_bootAppShaDiag[2] = BOOT_SHA_ERR_NVM_INIT;
        return 0U;
    }

    now = Boot_TimeGet();
    g_bootAppShaTime[2] = now - last;
    last = now;

    if (FALSE != Nvm_GetBlock3Invalid())
    {
        g_bootAppShaDiag[2] = BOOT_SHA_ERR_BLOCK;
        return 0U;
    }

    for (i = 0U; i < HSE_HASH_SHA256_SIZE; i++)
    {
        stored[i] = Nvm_Block3_Read_U8((Block3Data)(kBlock3_Now_SHAdata + i));
    }

    size = Boot_NvmReadU32((unsigned int)kBlock3_Now_FileSize);
    g_bootAppShaDiag[1] = size;

    if ((0U == size) || (size > BOOT_APP_SIZE))
    {
        g_bootAppShaDiag[2] = BOOT_SHA_ERR_SIZE;
        return 0U;
    }

    now = Boot_TimeGet();
    g_bootAppShaTime[3] = now - last;
    last = now;

    /* Still booting?  Then (and only then) wait, with a budget that covers
     * the remaining HSE start-up. */
    if ((0U == hseReady) && (0U == Boot_HseWaitReady()))
    {
        g_bootAppShaDiag[2] = BOOT_SHA_ERR_HSE_INIT;
        return 0U;
    }

    now = Boot_TimeGet();
    g_bootAppShaTime[4] = now - last;
    last = now;

    if (HSE_HASH_OK != HseHash_Sha256Region((const unsigned char *)BOOT_APP_BASE,
                                            size, digest))
    {
        g_bootAppShaDiag[2] = BOOT_SHA_ERR_HSE_HASH;
        g_bootAppShaDiag[3] = (unsigned int)HseHash_GetLastError();
        return 0U;
    }

    if (0 != memcmp(stored, digest, HSE_HASH_SHA256_SIZE))
    {
        g_bootAppShaDiag[2] = BOOT_SHA_ERR_MISMATCH;
        return 0U;
    }

    now = Boot_TimeGet();
    g_bootAppShaTime[5] = now - last;
    g_bootAppShaTime[6] = now;

    Boot_AppShaState    = BOOT_SHA_STATE_OK;
    g_bootAppShaDiag[0] = (unsigned int)BOOT_SHA_STATE_OK;

    return 1U;
}

void Boot_AppShaConfirm(void)
{
    /* Kept for compatibility.  The AB swap flow resets after 0xFF02, so the
     * check runs again from a clean state at the next boot. */
    Boot_AppShaState    = BOOT_SHA_STATE_OK;
    g_bootAppShaDiag[0] = (unsigned int)BOOT_SHA_STATE_OK;
    g_bootAppShaDiag[2] = 0U;
}

/* ---------------------------------------------------------------------------
 * Image header / vector table validation.
 *
 * The 16-byte header created by ELF2s19 at the image base holds the vector
 * table address (linked run address).  For a passive copy the image is stored
 * at a different address (+0x200000) but its content - including the vector
 * table and the reset handler - still uses the linked addresses, so the
 * vector table has to be read at (imageBase + vector - runBase).
 * ------------------------------------------------------------------------- */
unsigned char Boot_ImageHeaderValid(unsigned int imageBase, unsigned int runBase,
                                    unsigned int size)
{
    unsigned int magic;
    unsigned int vector;
    unsigned int entry;
    unsigned int sp;
    unsigned int reset;

    magic = *(volatile unsigned int *)imageBase;
    if (BOOT_APP_HEADER_MAGIC != magic)
    {
        return 0U;
    }

    vector = *(volatile unsigned int *)(imageBase + 12UL);
    if ((vector < (runBase + 0x2000UL)) ||
        (vector >= (runBase + size)) ||
        (0U != (vector & 3UL)))
    {
        return 0U;
    }

    entry = imageBase + (vector - runBase);
    sp = *(volatile unsigned int *)entry;
    reset = *(volatile unsigned int *)(entry + 4UL);

    if ((sp < 0x20000000UL) || (sp > 0x20444000UL))
    {
        return 0U;
    }

    if ((reset < runBase) || (reset >= (runBase + size)) ||
        (0U == (reset & 1UL)))
    {
        return 0U;
    }

    return 1U;
}

/* Debug view of the passive BOOT check (J-Link):
 *   [0] result 1 = valid    [1] length stored in NVM (kBlock3_PreBootSize)
 *   [2] failure reason      [3] 0x50425356 ("PBSV" marker) */
volatile unsigned int g_bootPassiveBootDiag[4] = {
    0U, 0U, 0U, 0x50425356UL
};

/* Reasons of a refused passive BOOT image. */
#define BOOT_PBSV_ERR_HEADER    0x200U   /* header / vector table invalid      */
#define BOOT_PBSV_ERR_SIZE      0x201U   /* stored length 0 or too large       */
#define BOOT_PBSV_ERR_HASH      0x202U   /* HSE SHA-256 failed                 */
#define BOOT_PBSV_ERR_MISMATCH  0x203U   /* digest differs from the NVM value  */

/* Is the BOOT image in the passive partition the one that was stored in NVM
 * block 3 (kBlock3_PreBootSHAdata / kBlock3_PreBootSize)?
 *
 * 0xFF02 / 0x31 01 AC C1 refuse to swap the partitions unless the BOOT image
 * that is about to become the running one is the verified one, so a corrupted
 * or interrupted BOOT download cannot brick the ECU.  A BOOT that was just
 * downloaded in this session is checked against the 0x37 digest instead (see
 * BootProg_DependencyCheck / BootProg_SwapStep); this function covers the
 * "no new BOOT" case (BOOT-only rollback, APP-only update).
 *
 * The whole image is hashed with the HSE, so the check also catches a flash
 * programming error that the in-session digest could not see. */
unsigned char Boot_PassiveBootValid(void)
{
    unsigned char stored[HSE_HASH_SHA256_SIZE];
    unsigned char digest[HSE_HASH_SHA256_SIZE];
    unsigned int size;
    unsigned int i;

    g_bootPassiveBootDiag[0] = 0U;
    g_bootPassiveBootDiag[2] = 0U;

    if (0U == Boot_ImageHeaderValid(BOOT_CODE_BACKUP_BASE, BOOT_CODE_BASE,
                                    BOOT_CODE_BACKUP_SIZE))
    {
        g_bootPassiveBootDiag[2] = BOOT_PBSV_ERR_HEADER;
        return 0U;
    }

    for (i = 0U; i < HSE_HASH_SHA256_SIZE; i++)
    {
        stored[i] = Nvm_Block3_Read_U8((Block3Data)(kBlock3_PreBootSHAdata + i));
    }

    size = Boot_NvmReadU32((unsigned int)kBlock3_PreBootSize);
    g_bootPassiveBootDiag[1] = size;

    /* A never programmed entry reads as 0xFF, so size becomes 0xFFFFFFFF. */
    if ((0U == size) || (size > BOOT_CODE_BACKUP_SIZE))
    {
        g_bootPassiveBootDiag[2] = BOOT_PBSV_ERR_SIZE;
        return 0U;
    }

    if (HSE_HASH_OK != HseHash_Sha256Region(
            (const unsigned char *)BOOT_CODE_BACKUP_BASE, size, digest))
    {
        g_bootPassiveBootDiag[2] = BOOT_PBSV_ERR_HASH;
        return 0U;
    }

    if (0 != memcmp(stored, digest, HSE_HASH_SHA256_SIZE))
    {
        g_bootPassiveBootDiag[2] = BOOT_PBSV_ERR_MISMATCH;
        return 0U;
    }

    g_bootPassiveBootDiag[0] = 1U;
    return 1U;
}

unsigned char Boot_PassiveAppValid(void)
{
    return Boot_ImageHeaderValid(BOOT_APP_BACKUP_BASE, BOOT_APP_BASE,
                                 BOOT_APP_SIZE);
}

unsigned char Boot_AppIsValid(void)
{
    if (0U == Boot_ImageHeaderValid(BOOT_APP_BASE, BOOT_APP_BASE,
                                    BOOT_APP_SIZE))
    {
        return 0U;
    }

    /* The SHA-256 stored by 0xFF02 did not match the run area: the header of
     * the image may look fine, but it must not be started. */
    if (BOOT_SHA_STATE_BAD == Boot_AppShaState)
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
    Boot_EarlyAbSwap = *(volatile unsigned int *)BOOT_RAM_AB_SWAP_ADDR;

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
 * no CAN, no GPT).  Only the core state needed to start the APP is set up.
 *
 * The APP may only be started when its SHA-256 matches the value stored in
 * NVM block 3 by 0xFF02.  This needs the NVM stack (MemAcc/Fee/
 * Mem_43_INFLS) and the HSE, i.e. a small part of the BSW bring-up; a check
 * that fails keeps the bootloader alive (Boot_AppShaState = "bad" makes
 * Boot_AppIsValid() fail from now on). */
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

    /* Reset that follows an AB swap: HSE FW releases up to 0.2.55 copy the
     * HSE firmware into the new passive partition and need about 1 s before
     * HSE_STATUS_INIT_OK.  The clock tree must not reconfigure HSE_CLK while
     * the HSE is busy, so wait here - before Boot_AppShaVerify() runs the
     * clock initialisation. */
    if (BOOT_RAM_AB_SWAP_MAGIC == Boot_EarlyAbSwap)
    {
        (void)HseHash_WaitReady(BOOT_HSE_SWAP_RETRIES, BOOT_HSE_INIT_DELAY);
        Boot_AbSwapFlagClear();
        __asm volatile ("dsb" ::: "memory");
    }

    /* Hash the run area with the HSE and compare it with the NVM values. */
    if (0U == Boot_AppShaVerify())
    {
        return;
    }

    vector = Boot_AppVector();
    sp = *(volatile unsigned int *)vector;
    reset = *(volatile unsigned int *)(vector + 4UL);

    __asm volatile ("cpsid i" ::: "memory");

    /* No driver may stay executable after the bootloader leaves.  The APP
     * startup copies its own ITCM text over 0x00000000 afterwards. */
    Boot_FlashWipe();

    /* No MCAL driver has been initialised, except the memory stack that was
     * needed for the SHA-256 check (see Boot_JumpToApp(), which does the same
     * after a programming session). */
    Mem_43_INFLS_DeInit();
    MemAcc_DeInit();

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

    Boot_StartApp(sp, reset);
}
void Boot_JumpToApp(void)
{
    unsigned int vector = Boot_AppVector();
    unsigned int sp = *(volatile unsigned int *)vector;
    unsigned int reset = *(volatile unsigned int *)(vector + 4UL);
    unsigned int i;

    /* De-initialise everything the bootloader owns before the APP starts. */
    __asm volatile ("cpsid i" ::: "memory");

    /* QCC 04.171: the RAM/ITCM driver must not remain executable. */
    BootProg_DestroyFlash();

    /* Stop the boot loader time base and clear its pending flag: a stale PIT
     * interrupt would otherwise hit the APP before it installs its own
     * PIT0 handler (default handler -> endless loop). */
    Gpt_StopTimer(GptConf_GptChannelConfiguration_GptChannelConfiguration_PIT0_CH0);
    Gpt_DeInit();
    Can_43_FLEXCAN_DeInit();
    Mcl_DeInit();
    Mem_43_INFLS_DeInit();
    MemAcc_DeInit();


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
    Boot_StartApp(sp, reset);
}



















