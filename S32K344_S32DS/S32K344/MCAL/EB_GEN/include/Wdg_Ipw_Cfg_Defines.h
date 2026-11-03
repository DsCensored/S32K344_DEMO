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


#ifndef WDG_IPW_CFG_DEFINES_H
#define WDG_IPW_CFG_DEFINES_H

/**
*   @file
*
*   @addtogroup  Wdg
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
#include "Swt_Ip_Cfg_Defines.h"
/*==================================================================================================
                                SOURCE FILE VERSION INFORMATION
==================================================================================================*/

#define WDG_IPW_CFG_DEFINES_VENDOR_ID                    43
#define WDG_IPW_CFG_DEFINES_AR_RELEASE_MAJOR_VERSION     4
#define WDG_IPW_CFG_DEFINES_AR_RELEASE_MINOR_VERSION     7
#define WDG_IPW_CFG_DEFINES_AR_RELEASE_REVISION_VERSION  0
#define WDG_IPW_CFG_DEFINES_SW_MAJOR_VERSION             5
#define WDG_IPW_CFG_DEFINES_SW_MINOR_VERSION             0
#define WDG_IPW_CFG_DEFINES_SW_PATCH_VERSION             0

/*==================================================================================================
                                      FILE VERSION CHECKS
==================================================================================================*/


#if (WDG_IPW_CFG_DEFINES_VENDOR_ID != SWT_IP_CFG_DEFINES_VENDOR_ID)
#error "Wdg_Ipw_Cfg_Defines.h and Swt_Ip_Cfg_Defines.h have different vendor ids"
#endif

/* Check if current file and Swt_Ip_Cfg_Defines header file are of the same Autosar version */
#if ((WDG_IPW_CFG_DEFINES_AR_RELEASE_MAJOR_VERSION   != SWT_IP_CFG_DEFINES_AR_RELEASE_MAJOR_VERSION) || \
(WDG_IPW_CFG_DEFINES_AR_RELEASE_MINOR_VERSION    != SWT_IP_CFG_DEFINES_AR_RELEASE_MINOR_VERSION) || \
(WDG_IPW_CFG_DEFINES_AR_RELEASE_REVISION_VERSION != SWT_IP_CFG_DEFINES_AR_RELEASE_REVISION_VERSION) \
)
#error "AutoSar Version Numbers of Wdg_Ipw_Cfg_Defines.h and Swt_Ip_Cfg_Defines.h are different"
#endif

/* Check if current file and Swt_Ip_Cfg_Defines header file are of the same software version */
#if ((WDG_IPW_CFG_DEFINES_SW_MAJOR_VERSION != SWT_IP_CFG_DEFINES_SW_MAJOR_VERSION) || \
(WDG_IPW_CFG_DEFINES_SW_MINOR_VERSION  != SWT_IP_CFG_DEFINES_SW_MINOR_VERSION) || \
(WDG_IPW_CFG_DEFINES_SW_PATCH_VERSION  != SWT_IP_CFG_DEFINES_SW_PATCH_VERSION) \
)
#error "Software Version Numbers of Wdg_Ipw_Cfg_Defines.h and Swt_Ip_Cfg_Defines.h are different"
#endif


/*==================================================================================================
*                                            CONSTANTS
==================================================================================================*/

/*==================================================================================================
*                                       DEFINES AND MACROS
==================================================================================================*/


/**
 * @brief  This define indicate the number of instances of the WDG
 */
#define WDG_IPW_NO_OF_INSTANCES            (1U)

/**
 * @brief  This define initializes all type of wdg instances to WDG_IPW_INIT_ARRAY
 */
#define WDG_IPW_INIT_ARRAY {WDG_IPW_UNINIT_IP }

/**
  * @brief  This constant specifies if the instance of the WDG is selected or not
  */
#define WDG_IPW_ENABLE_INSTANCE0      (SWT_IP_ENABLE_INSTANCE0)

/**
  * @brief  This constant specifies the hardware used or not.
  */
#define SWT_IP_USED      (STD_ON)
#define AEWDOG_IP_USED   (STD_OFF)

/**
* @brief  Compile switch to allow/forbid disabling the watchdog driver during runtime
*/
#define WDG_IPW_DEINIT (STD_ON)


/**
* @brief  This variable will which Wdg instances can be serviced directly
*/
#define WDG_IPW_ENABLE_DIRECT_SERVICE_INSTANCE0 (SWT_IP_ENABLE_DIRECT_SERVICE_INSTANCE0)

#define WDG_IPW_DIRECT_SERVICE  (SWT_IP_ENABLE_DIRECT_SERVICE)


/**
* @brief  This variable will indicate if the ISR for WDG instances is used
*/
#define WDG_IPW_ISR0_USED (SWT_IP_ISR0_USED)

/**
* @brief  This variable will indicate which Wdg instance support the Clear Reset Request feature
*/
#define WDG_IPW_CLEAR_RESET_REQUEST_INSTANCE0 (SWT_IP_CLEAR_RESET_REQUEST_INSTANCE0)
#define WDG_IPW_CLEAR_RESET_REQUEST (SWT_IP_CLEAR_RESET_REQUEST)


/**
* @brief  These variables will indicate the Wdg Initial Timeout and Wdg Max Timeout parameters in miliseconds for Wdg module 0
*/
#define WDG_IPW_INSTANCE0_INITIAL_TIMEOUT_U16 (SWT_IP_INSTANCE0_INITIAL_TIMEOUT_U16)
#define WDG_IPW_INSTANCE0_MAX_TIMEOUT_U16 (SWT_IP_INSTANCE0_MAX_TIMEOUT_U16)



/**
* @brief  Compile switch to enable development error detection
*/

#define WDG_IPW_DEV_ERROR_DETECT (STD_ON)


#ifdef __cplusplus
}
#endif

/** @} */

#endif /* WDG_IPW_CFG_DEFINES_H */
