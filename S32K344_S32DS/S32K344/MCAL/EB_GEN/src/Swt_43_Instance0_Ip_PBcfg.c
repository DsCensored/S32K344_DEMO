/*==================================================================================================
*   Project              : RTD AUTOSAR 4.7
*   Platform             : CORTEXM
*   Peripheral           : Swt
*   Dependencies         : none
*
*   Autosar Version      : 4.7.0
*   Autosar Revision     : ASR_REL_4_7_REV_0000
*   Autosar Conf.Variant :
*   SW Version           : 5.0.0
*   Build Version        : S32K3_RTD_5_0_0_D2408_ASR_REL_4_7_REV_0000_20241002
*
*   Copyright 2020 - 2024 NXP
*
*   NXP Confidential and Proprietary. This software is owned or controlled by NXP and may only be used strictly
*   in accordance with the applicable license terms.  By expressly accepting
*   such terms or by downloading, installing, activating and/or otherwise using
*   the software, you are agreeing that you have read, and that you agree to
*   comply with and are bound by, such license terms.  If you do not agree to
*   be bound by the applicable license terms, then you may not retain,
*   install, activate or otherwise use the software.
==================================================================================================*/

/**
* @file
*
* @addtogroup  Swt
* @{
*/

#ifdef __cplusplus
extern "C"{
#endif

/*==================================================================================================
*                                        INCLUDE FILES
* 1) system and project includes
* 2) needed interfaces from external units
* 3) internal and external interfaces from this unit
==================================================================================================*/

#include "Swt_43_Instance0_Ip_PBcfg.h"




















 
/*==================================================================================================
*                                 SOURCE FILE VERSION INFORMATION
==================================================================================================*/
#define SWT_43_INSTANCE0_IP_PBCFG_VENDOR_ID_C                     43
#define SWT_43_INSTANCE0_IP_PBCFG_AR_RELEASE_MAJOR_VERSION_C      4
#define SWT_43_INSTANCE0_IP_PBCFG_AR_RELEASE_MINOR_VERSION_C      7
#define SWT_43_INSTANCE0_IP_PBCFG_AR_RELEASE_REVISION_VERSION_C   0
#define SWT_43_INSTANCE0_IP_PBCFG_SW_MAJOR_VERSION_C              5
#define SWT_43_INSTANCE0_IP_PBCFG_SW_MINOR_VERSION_C              0
#define SWT_43_INSTANCE0_IP_PBCFG_SW_PATCH_VERSION_C              0

/*==================================================================================================
*                                       FILE VERSION CHECKS
==================================================================================================*/
/* Check if current file and Swt_43_Instance0_Ip_PBcfg header file are of the same vendor */
#if (SWT_43_INSTANCE0_IP_PBCFG_VENDOR_ID_C != SWT_43_INSTANCE0_IP_VENDOR_ID_PBCFG)
#error "Swt_43_Instance0_Ip_PBcfg.c and Swt_43_Instance0_Ip_PBcfg.h have different vendor ids"
#endif

/* Check if current file and Swt_43_Instance0_Ip_PBcfg header file are of the same Autosar version */
#if ((SWT_43_INSTANCE0_IP_PBCFG_AR_RELEASE_MAJOR_VERSION_C    != SWT_43_INSTANCE0_IP_AR_RELEASE_MAJOR_VERSION_PBCFG   ) || \
     (SWT_43_INSTANCE0_IP_PBCFG_AR_RELEASE_MINOR_VERSION_C    != SWT_43_INSTANCE0_IP_AR_RELEASE_MINOR_VERSION_PBCFG   ) || \
     (SWT_43_INSTANCE0_IP_PBCFG_AR_RELEASE_REVISION_VERSION_C != SWT_43_INSTANCE0_IP_AR_RELEASE_REVISION_VERSION_PBCFG) \
    )
#error "AutoSar Version Numbers of Swt_43_Instance0_Ip_PBcfg.c and Swt_43_Instance0_Ip_PBcfg.h are different"
#endif

/* Check if current file and Swt_43_Instance0_Ip_PBcfg header file are of the same software version */
#if ((SWT_43_INSTANCE0_IP_PBCFG_SW_MAJOR_VERSION_C != SWT_43_INSTANCE0_IP_SW_MAJOR_VERSION_PBCFG) || \
     (SWT_43_INSTANCE0_IP_PBCFG_SW_MINOR_VERSION_C != SWT_43_INSTANCE0_IP_SW_MINOR_VERSION_PBCFG) || \
     (SWT_43_INSTANCE0_IP_PBCFG_SW_PATCH_VERSION_C != SWT_43_INSTANCE0_IP_SW_PATCH_VERSION_PBCFG) \
    )
#error "Software Version Numbers of Swt_43_Instance0_Ip_PBcfg.c and Swt_43_Instance0_Ip_PBcfg.h are different"
#endif



/*==================================================================================================
*                           LOCAL TYPEDEFS (STRUCTURES, UNIONS, ENUMS)
==================================================================================================*/

/*==================================================================================================
*                                          LOCAL MACROS
==================================================================================================*/

/*==================================================================================================
*                                         LOCAL CONSTANTS
==================================================================================================*/

/*==================================================================================================
*                                         LOCAL VARIABLES
==================================================================================================*/

/*==================================================================================================
*                                        GLOBAL CONSTANTS
==================================================================================================*/

/*==================================================================================================
*                                        GLOBAL VARIABLES
==================================================================================================*/

#define WDG_START_SEC_CONFIG_DATA_UNSPECIFIED

#include "Wdg_MemMap.h"

/**
 *   @brief External Notifications for Wdg Interrupt
 */
extern void Wdg_CallBackNotification0(void);

const Swt_Ip_ConfigType Swt_Ip_OffModeSettings_0=
{
    (uint8)           SWT_IP_ALL_MAP_ENABLE, /* u8MapEnBitmask */
    (boolean)         FALSE,        /* bEnResetOnInvalidAccess */
#if (defined(SWT_IP_HAS_STOP_MODE) && (SWT_IP_HAS_STOP_MODE == 1U))
    (boolean)         FALSE,        /* bEnRunInStopMode */
#endif
    (boolean)         FALSE,        /* bEnRunInDebugMode */
    (boolean)         FALSE,        /* bEnWindow */
    (boolean)         FALSE,        /* bEnInterrupt */
    (Swt_Ip_ServiceModeType)     SWT_IP_FS_SEQ_MODE,        /* eServiceMode */
    (uint32)          0x100,    /* u32TimeoutValue */
    (uint32)          0x0,        /* u32WindowValue */
    (uint16)          0x0,        /* u16InitialKey */
    (Swt_Ip_LockType)SWT_IP_SOFTLOCK, /* lockConfig */
    NULL_PTR     /* pfSwtCallback */
};

const Swt_Ip_ConfigType Swt_Ip_SlowModeSettings_0=
{
    (uint8)           (SWT_IP_ALL_MAP_DISABLE | SWT_IP_MAP0_ENABLE | SWT_IP_MAP1_ENABLE | SWT_IP_MAP2_ENABLE | SWT_IP_MAP3_ENABLE | SWT_IP_MAP4_ENABLE | SWT_IP_MAP5_ENABLE | SWT_IP_MAP6_ENABLE | SWT_IP_MAP7_ENABLE),  /* u8MapEnBitmask */
    (boolean)         TRUE,         /* bEnResetOnInvalidAccess */
#if (defined(SWT_IP_HAS_STOP_MODE) && (SWT_IP_HAS_STOP_MODE == 1U))
    (boolean)         FALSE,        /* bEnRunInStopMode */
#endif
    (boolean)         FALSE,         /* bEnRunInDebugMode */
    (boolean)         FALSE,        /* bEnWindow */
    (boolean)         TRUE,        /* bEnInterrupt */
    
    (Swt_Ip_ServiceModeType)         SWT_IP_FS_SEQ_MODE,        /* eServiceMode */
    
    (uint32)          0x0004e200,      /* u32TimeoutValue */
    (uint32)          0x0,          /* u32WindowValue */
    (uint16)          0x0000,          /* u16InitialKey */
    (Swt_Ip_LockType) SWT_IP_SOFTLOCK,     /* lockConfig */
    (Swt_CallbackPtrType) &Wdg_CallBackNotification0 /* pfSwtCallback */
};

const Swt_Ip_ConfigType Swt_Ip_FastModeSettings_0=
{
    (uint8)           (SWT_IP_ALL_MAP_DISABLE | SWT_IP_MAP0_ENABLE | SWT_IP_MAP1_ENABLE | SWT_IP_MAP2_ENABLE | SWT_IP_MAP3_ENABLE | SWT_IP_MAP4_ENABLE | SWT_IP_MAP5_ENABLE | SWT_IP_MAP6_ENABLE | SWT_IP_MAP7_ENABLE),  /* u8MapEnBitmask */
    (boolean)         TRUE,         /* bEnResetOnInvalidAccess */
#if (defined(SWT_IP_HAS_STOP_MODE) && (SWT_IP_HAS_STOP_MODE == 1U))
    (boolean)         FALSE,        /* bEnRunInStopMode */
#endif
    (boolean)         FALSE,         /* bEnRunInDebugMode */
    (boolean)         FALSE,        /* bEnWindow */
    (boolean)         TRUE,        /* bEnInterrupt */
    
    (Swt_Ip_ServiceModeType)         SWT_IP_FS_SEQ_MODE,        /* eServiceMode */
    
    (uint32)          0x0004e200,      /* u32TimeoutValue */
    (uint32)          0x0,          /* u32WindowValue */
    (uint16)          0x0000,          /* u16InitialKey */
    (Swt_Ip_LockType) SWT_IP_SOFTLOCK,     /* lockConfig */
    (Swt_CallbackPtrType) &Wdg_CallBackNotification0 /* pfSwtCallback */
};



#define WDG_STOP_SEC_CONFIG_DATA_UNSPECIFIED

#include "Wdg_MemMap.h"

/*==================================================================================================
*                                    LOCAL FUNCTION PROTOTYPES
==================================================================================================*/

/*==================================================================================================
*                                         LOCAL FUNCTIONS
==================================================================================================*/

/*==================================================================================================
*                                        GLOBAL FUNCTIONS
==================================================================================================*/

#ifdef __cplusplus
}
#endif

/** @} */

