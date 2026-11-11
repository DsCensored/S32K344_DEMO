/*
 * App_Info.c
 *
 * Fixed-address information blocks of the application image.
 * The linker script places each array at its own absolute address, see
 * App_Info.h and the .app_*_flash sections in S32K344_flash.ld.
 */

#include <string.h>

#include "App_Info.h"

/* 0x00448100 - application version */
__attribute__((section(".app_ver_flash"), used))
const uint8 App_Info_AppVersion[APP_INFO_BLOCK_SIZE] = "1.1.0";

/* 0x00448110 - bootloader version */
__attribute__((section(".app_boot_ver_flash"), used))
const uint8 App_Info_BootVersion[APP_INFO_BLOCK_SIZE] = "HW_0.0.1";

/* 0x00448120 - hardware / board version */
__attribute__((section(".app_hw_ver_flash"), used))
const uint8 App_Info_HardwareVersion[APP_INFO_BLOCK_SIZE] = "NXP-S32K344-Q172";

/* 0x00448130 - order / part number */
__attribute__((section(".app_order_flash"), used))
const uint8 App_Info_OrderCode[APP_INFO_BLOCK_SIZE] = "ORD-DEMO-0001";

/* 0x00448140 - supplier code */
__attribute__((section(".app_supplier_flash"), used))
const uint8 App_Info_SupplierCode[APP_INFO_BLOCK_SIZE] = "SUP-NXP";

/* 0x00448150 - serial number */
__attribute__((section(".app_serial_flash"), used))
const uint8 App_Info_SerialNumber[APP_INFO_BLOCK_SIZE] = "SN-0000000001";

/* 0x00448160 - production date */
__attribute__((section(".app_prod_date_flash"), used))
const uint8 App_Info_ProductionDate[APP_INFO_BLOCK_SIZE] = __DATE__;

/* 0x00448170 - software update component id */
__attribute__((section(".app_upd_comp_flash"), used))
const uint8 App_Info_UpdateComponent[APP_INFO_BLOCK_SIZE] = __TIME__;

static const uint8 *const App_Info_Blocks[APP_INFO_BLOCK_COUNT] =
{
    App_Info_AppVersion,
    App_Info_BootVersion,
    App_Info_HardwareVersion,
    App_Info_OrderCode,
    App_Info_SupplierCode,
    App_Info_SerialNumber,
    App_Info_ProductionDate,
    App_Info_UpdateComponent
};

uint32 App_Info_GetAddress(App_InfoIdType id)
{
    if ((uint32)id >= (uint32)APP_INFO_BLOCK_COUNT)
    {
        return 0UL;
    }

    return (uint32)&App_Info_Blocks[(uint32)id][0];
}

const uint8 *App_Info_Get(App_InfoIdType id)
{
    if ((uint32)id >= (uint32)APP_INFO_BLOCK_COUNT)
    {
        return NULL_PTR;
    }

    return App_Info_Blocks[(uint32)id];
}

Std_ReturnType App_Info_GetCopy(App_InfoIdType id, uint8 *dst)
{
    if (((uint32)id >= (uint32)APP_INFO_BLOCK_COUNT) || (NULL_PTR == dst))
    {
        return E_NOT_OK;
    }

    (void)memcpy(dst, App_Info_Blocks[(uint32)id], APP_INFO_BLOCK_SIZE);
    return E_OK;
}
