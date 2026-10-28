/*
 * Boot_Flash.h
 *
 * Access to the flash driver that the tester downloads into ITCM.
 */

#ifndef BOOT_FLASH_H_
#define BOOT_FLASH_H_

#define BOOT_DRIVER_BASE        0x00000000UL   /* ITCM */
#define BOOT_DRIVER_MAX_SIZE    0x00008000UL   /* 32 KB download window */
#define BOOT_DRIVER_MAGIC       0x48534C46UL   /* 'F' 'L' 'S' 'H' */
#define BOOT_DRIVER_VERSION     0x00010001UL

/* Last error returned by the driver (for debugging); 0 = OK. */
extern int Boot_FlashLastError;

unsigned char Boot_FlashDriverValid(void);
unsigned char Boot_FlashInit(void);
unsigned char Boot_FlashDeInit(void);
unsigned char Boot_FlashErase(unsigned int addr, unsigned int len);
unsigned char Boot_FlashWrite(unsigned int addr, const unsigned char *buf,
                              unsigned int len);
unsigned char Boot_FlashVerify(unsigned int addr, const unsigned char *buf,
                               unsigned int len);

#endif /* BOOT_FLASH_H_ */
