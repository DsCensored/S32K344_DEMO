/*
 * Boot_Program.h
 *
 * UDS programming services of the bootloader:
 *   - 0x34/0x36/0x37 download of the flash driver (ITCM) and of the APP
 *     (backup area),
 *   - dependency check (APP + flash driver),
 *   - erase of a given address range, one sector per DCM poll,
 *   - 8 KB block copy from the backup area to the run area.
 */

#ifndef BOOT_PROGRAM_H_
#define BOOT_PROGRAM_H_

#define BOOT_PROG_MAX_BLOCK     250U      /* data bytes per 0x36 request */
#define BOOT_PROG_COPY_CHUNK    0x2000U   /* 8 KB per DCM poll */

unsigned char BootProg_RequestDownload(unsigned int addr, unsigned int size,
                                       unsigned int *maxBlock);
unsigned char BootProg_TransferData(unsigned char seq, const unsigned char *data,
                                    unsigned int len);
unsigned char BootProg_TransferExit(unsigned int *crc);

/* Erase one 8 KB sector per call; *more is set when the DCM must call again
 * (the handler then returns NRC 0x78). */
unsigned char BootProg_EraseStep(unsigned int addr, unsigned int len,
                                 unsigned int *status, unsigned char *more);
unsigned char BootProg_DependencyCheck(unsigned int *status);
unsigned char BootProg_CopyStep(unsigned int *status, unsigned char *more);

/* QCC 04.171 31 01 02 02: compare the tester checksum with the CRC of the
 * last downloaded block.  *status = 0 (correct) / 1 (incorrect). */
unsigned char BootProg_CheckIntegrity(unsigned int expected, unsigned int *status);

/* QCC 04.171 2E F1 84: fingerprint interface.  Fee is not integrated yet,
 * so the current implementation keeps the 9 bytes in RAM. */
unsigned char BootProg_WriteFingerprint(const unsigned char *data);
unsigned char BootProg_ReadFingerprint(unsigned char *data);

#endif /* BOOT_PROGRAM_H_ */
