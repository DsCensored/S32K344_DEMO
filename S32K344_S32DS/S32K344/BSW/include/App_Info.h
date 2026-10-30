/*
 * App_Info.h
 *
 * Fixed-address information blocks of the application and of the bootloader.
 *
 * Eight 16-byte arrays are placed at fixed flash addresses by the linker
 * script (see S32K344_flash.ld of both projects), so that a programming tool,
 * the bootloader or the application itself can always find the same
 * information at the same address, independently of the code layout:
 *
 *      APP image       BOOT image      array
 *      0x00448100      0x00400100      App_Info_AppVersion
 *      0x00448110      0x00400110      App_Info_BootVersion
 *      0x00448120      0x00400120      App_Info_HardwareVersion
 *      0x00448130      0x00400130      App_Info_OrderCode
 *      0x00448140      0x00400140      App_Info_SupplierCode
 *      0x00448150      0x00400150      App_Info_SerialNumber
 *      0x00448160      0x00400160      App_Info_ProductionDate
 *      0x00448170      0x00400170      App_Info_UpdateComponent
 *
 * Every block is exactly APP_INFO_BLOCK_SIZE (16) bytes; unused bytes are
 * zero.  The blocks are part of the image (and therefore of the generated
 * S19), they can be read at run time or patched by the flasher.
 */

#ifndef APP_INFO_H
#define APP_INFO_H

#include "Std_Types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Size of one fixed information block in bytes. */
#define APP_INFO_BLOCK_SIZE       (16U)

/** Number of fixed information blocks. */
#define APP_INFO_BLOCK_COUNT      (8U)

/** Base address of the fixed information area in the APP image. */
#define APP_INFO_BASE_ADDRESS     (0x00448100UL)

/** Base address of the fixed information area in the BOOT image. */
#define APP_INFO_BOOT_BASE_ADDRESS (0x00400100UL)

/** Address of one information block of the APP image. */
#define APP_INFO_ADDRESS(id)      (APP_INFO_BASE_ADDRESS + ((uint32)(id) * APP_INFO_BLOCK_SIZE))

/** Address of one information block of the BOOT image. */
#define APP_INFO_BOOT_ADDRESS(id) (APP_INFO_BOOT_BASE_ADDRESS + ((uint32)(id) * APP_INFO_BLOCK_SIZE))

/**
 * @brief Fixed information blocks, in address order.
 */
typedef enum
{
    APP_INFO_APP_VERSION = 0,   /**< 0x00448100 application version           */
    APP_INFO_BOOT_VERSION,      /**< 0x00448110 bootloader version            */
    APP_INFO_HARDWARE_VERSION,  /**< 0x00448120 hardware / board version      */
    APP_INFO_ORDER_CODE,        /**< 0x00448130 order / part number           */
    APP_INFO_SUPPLIER_CODE,     /**< 0x00448140 supplier code                 */
    APP_INFO_SERIAL_NUMBER,     /**< 0x00448150 serial number                 */
    APP_INFO_PRODUCTION_DATE,   /**< 0x00448160 production date (YYYY-MM-DD)  */
    APP_INFO_UPDATE_COMPONENT,  /**< 0x00448170 software update component id  */
    APP_INFO_INVALID = 0xFF
} App_InfoIdType;

/* Fixed blocks (defined in App_Info.c, placed by the linker). */
extern const uint8 App_Info_AppVersion[APP_INFO_BLOCK_SIZE];
extern const uint8 App_Info_BootVersion[APP_INFO_BLOCK_SIZE];
extern const uint8 App_Info_HardwareVersion[APP_INFO_BLOCK_SIZE];
extern const uint8 App_Info_OrderCode[APP_INFO_BLOCK_SIZE];
extern const uint8 App_Info_SupplierCode[APP_INFO_BLOCK_SIZE];
extern const uint8 App_Info_SerialNumber[APP_INFO_BLOCK_SIZE];
extern const uint8 App_Info_ProductionDate[APP_INFO_BLOCK_SIZE];
extern const uint8 App_Info_UpdateComponent[APP_INFO_BLOCK_SIZE];

/**
 * @brief Get the fixed flash address of one information block.
 * @param id Block id.
 * @return Address in program flash, 0 if the id is invalid.
 */
uint32 App_Info_GetAddress(App_InfoIdType id);

/**
 * @brief Get a pointer to one information block (read only).
 * @param id Block id.
 * @return Pointer to 16 bytes, NULL_PTR if the id is invalid.
 */
const uint8 *App_Info_Get(App_InfoIdType id);

/**
 * @brief Copy one information block into a caller buffer.
 * @param id  Block id.
 * @param dst Destination, must hold at least APP_INFO_BLOCK_SIZE bytes.
 * @return E_OK on success, E_NOT_OK on invalid parameter.
 */
Std_ReturnType App_Info_GetCopy(App_InfoIdType id, uint8 *dst);

#ifdef __cplusplus
}
#endif

#endif /* APP_INFO_H */
