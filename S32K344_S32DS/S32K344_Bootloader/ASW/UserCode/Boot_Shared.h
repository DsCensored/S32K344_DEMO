/*
 * Boot_Shared.h
 *
 * APP <-> Bootloader handshake (same mechanism as the reference project
 * DcmAppl_Switch_DcmBootLoaderReset.c / app_jump_boot()).
 *
 * The APP sends 7F 10 78 itself, de-initialises the peripherals, writes
 * 0xA5A5A5A5 to 0x20443FF0 and calls Mcu_PerformReset().  The bootloader
 * reads the same word after the functional reset.
 */

#ifndef BOOT_SHARED_H_
#define BOOT_SHARED_H_

#include <stdint.h>

#define BOOT_RAM_FLAG_ADDR              0x20443FF0UL
#define BOOT_RAM_FLAG_AUX_ADDR          0x20443FF4UL
#define BOOT_RAM_FLAG_MAGIC             0xA5A5A5A5UL

#define BOOT_RAM_REQUEST_NONE           0x00000000UL
#define BOOT_RAM_REQUEST_PROGRAMMING    0x00000001UL

/* APP side: write the magic that makes the bootloader stay in programming. */
static inline void Boot_RamFlagSet(uint32_t request)
{
    (void)request;

    *(volatile uint32_t *)BOOT_RAM_FLAG_ADDR = BOOT_RAM_FLAG_MAGIC;
    *(volatile uint32_t *)BOOT_RAM_FLAG_AUX_ADDR = 0UL;
}

/* Boot side: clear the magic. */
static inline void Boot_RamFlagClear(void)
{
    *(volatile uint32_t *)BOOT_RAM_FLAG_ADDR = 0UL;
    *(volatile uint32_t *)BOOT_RAM_FLAG_AUX_ADDR = 0UL;
}

/* Returns BOOT_RAM_REQUEST_PROGRAMMING when the magic is valid. */
static inline uint32_t Boot_RamFlagGet(void)
{
    if (BOOT_RAM_FLAG_MAGIC == *(volatile uint32_t *)BOOT_RAM_FLAG_ADDR)
    {
        return BOOT_RAM_REQUEST_PROGRAMMING;
    }

    return BOOT_RAM_REQUEST_NONE;
}

#endif /* BOOT_SHARED_H_ */
