/*
 * Boot_Program.h
 *
 * UDS programming services of the bootloader:
 *   - 0x34/0x36/0x37 download of the flash driver (ITCM) and of the APP
 *     (backup area),
 *   - dependency check (APP + flash driver),
 *   - erase of a given address range,
 *   - 8 KB block copy from the backup area to the run area.
 */

#ifndef BOOT_PROGRAM_H_
#define BOOT_PROGRAM_H_

#define BOOT_PROG_MAX_BLOCK     0x0800U   /* max data bytes per 0x36 request */
#define BOOT_PROG_COPY_CHUNK    0x2000U   /* 8 KB per DCM poll */

unsigned char BootProg_RequestDownload(unsigned int addr, unsigned int size,
                                       unsigned char *maxBlock);
unsigned char BootProg_TransferData(unsigned char seq, const unsigned char *data,
                                    unsigned int len);
unsigned char BootProg_TransferExit(unsigned int *crc);
unsigned char BootProg_Erase(unsigned int addr, unsigned int len,
                             unsigned int *status);
unsigned char BootProg_DependencyCheck(unsigned int *status);
unsigned char BootProg_CopyStep(unsigned int *status, unsigned char *more);

#endif /* BOOT_PROGRAM_H_ */
