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

#ifndef SWT_IP_CFG_H
#define SWT_IP_CFG_H

/**
*   @file
*
*   @addtogroup SWT_ip_config SWT IPL Configuration
*   @{
*/

#ifdef __cplusplus
extern "C"{
#endif


/*==================================================================================================
*                                          INCLUDE FILES
* 1) system and project includes
* 2) needed interfaces from external units
* 3) internal and external interfaces from this unit
==================================================================================================*/
#include "Std_Types.h"

#include "Swt_43_Instance0_Ip_PBcfg.h"

/*==================================================================================================
*                                 SOURCE FILE VERSION INFORMATION
==================================================================================================*/
#define SWT_IP_VENDOR_ID_CFG_H                    43
#define SWT_IP_AR_RELEASE_MAJOR_VERSION_CFG_H     4
#define SWT_IP_AR_RELEASE_MINOR_VERSION_CFG_H     7
#define SWT_IP_AR_RELEASE_REVISION_VERSION_CFG_H  0
#define SWT_IP_SW_MAJOR_VERSION_CFG_H             5
#define SWT_IP_SW_MINOR_VERSION_CFG_H             0
#define SWT_IP_SW_PATCH_VERSION_CFG_H             0

/*==================================================================================================
*                                       FILE VERSION CHECKS
==================================================================================================*/
#ifndef DISABLE_MCAL_INTERMODULE_ASR_CHECK
    /* Checks against Std_Types.h */
    #if ((SWT_IP_AR_RELEASE_MAJOR_VERSION_CFG_H != STD_AR_RELEASE_MAJOR_VERSION) || \
         (SWT_IP_AR_RELEASE_MINOR_VERSION_CFG_H != STD_AR_RELEASE_MINOR_VERSION) \
        )
        #error "AutoSar Version Numbers of Swt_Ip_Cfg.h and Std_Types.h are different"
    #endif
#endif

                            #if (SWT_IP_VENDOR_ID_CFG_H != SWT_43_INSTANCE0_IP_VENDOR_ID_PBCFG)
    #error "Swt_Ip_Cfg.h and Swt_43_Instance0_Ip_PBcfg.h have different vendor ids"
#endif

#if ((SWT_IP_AR_RELEASE_MAJOR_VERSION_CFG_H != SWT_43_INSTANCE0_IP_AR_RELEASE_MAJOR_VERSION_PBCFG) || \
     (SWT_IP_AR_RELEASE_MINOR_VERSION_CFG_H != SWT_43_INSTANCE0_IP_AR_RELEASE_MINOR_VERSION_PBCFG) || \
     (SWT_IP_AR_RELEASE_REVISION_VERSION_CFG_H != SWT_43_INSTANCE0_IP_AR_RELEASE_REVISION_VERSION_PBCFG) \
    )
    #error "AutoSar Version Numbers of Swt_Ip_Cfg.h and Swt_43_Instance0_Ip_PBcfg.h are different"
#endif

#if ((SWT_IP_SW_MAJOR_VERSION_CFG_H != SWT_43_INSTANCE0_IP_SW_MAJOR_VERSION_PBCFG) || \
     (SWT_IP_SW_MINOR_VERSION_CFG_H != SWT_43_INSTANCE0_IP_SW_MINOR_VERSION_PBCFG) || \
     (SWT_IP_SW_PATCH_VERSION_CFG_H != SWT_43_INSTANCE0_IP_SW_PATCH_VERSION_PBCFG) \
    )
    #error "Software Version Numbers of Swt_Ip_Cfg.h and Swt_43_Instance0_Ip_PBcfg.h are different"
#endif
            /*==================================================================================================
*                                            CONSTANTS
==================================================================================================*/

/*==================================================================================================
*                                       DEFINES AND MACROS
==================================================================================================*/

/*==================================================================================================
*                                              ENUMS
==================================================================================================*/

/*==================================================================================================
*                                  STRUCTURES AND OTHER TYPEDEFS
==================================================================================================*/

/*==================================================================================================
*                                  GLOBAL VARIABLE DECLARATIONS
==================================================================================================*/

/*==================================================================================================
*                                       FUNCTION PROTOTYPES
==================================================================================================*/


#ifdef __cplusplus
}
#endif

/** @} */

#endif /* SWT_IP_CFG_H */

