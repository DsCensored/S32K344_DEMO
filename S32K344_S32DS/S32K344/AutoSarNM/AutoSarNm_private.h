/*
 * File: AutoSarNm_private.h
 *
 * Code generated for Simulink model 'AutoSarNm'.
 *
 * Model version                  : 14.379
 * Simulink Coder version         : 9.8 (R2022b) 13-May-2022
 * C/C++ source code generated on : Mon Nov  9 10:17:00 2026
 *
 * Target selection: ert.tlc
 * Embedded hardware selection: ARM Compatible->ARM 7
 * Code generation objectives: Unspecified
 * Validation result: Not run
 */

#ifndef RTW_HEADER_AutoSarNm_private_h_
#define RTW_HEADER_AutoSarNm_private_h_
#include "rtwtypes.h"
#include "AutoSarNm_types.h"

/* Includes for objects with custom storage classes */
#include "NMmain.h"

/*
 * Check that imported macros with storage class "ImportedDefine" are defined
 */
#ifndef CAN_ID_VEH_TX_NM
#error The value of parameter "CAN_ID_VEH_TX_NM" is not defined
#endif

/* Imported (extern) block signals */
#if AUROSAR_NM_CANWAKEUP_ENABLE != 0

extern uint8_T AutoSarNM_cntNmMsgRxCnt;/* '<Root>/AutoSarNM_cntNmMsgRxCnt' */

#endif

extern uint8_T AutoSarNM_cntUDSMsgRxCnt;/* '<Root>/AutoSarNM_cntUDSMsgRxCnt' */
extern uint8_T BSW_stKL15;             /* '<Root>/BSW_stKL15' */
extern uint8_T AutoSarNM_stWKUPMode;   /* '<Root>/AutoSarNM_stWKUPMode' */
extern uint32_T CANNM_cntAuroSarNMFrameSendCnt;
                                   /* '<Root>/CANNM_cntAuroSarNMFrameSendCnt' */
extern uint32_T CANNM_cntAuroSarNMFrameRecvCnt;
                                   /* '<Root>/CANNM_cntAuroSarNMFrameRecvCnt' */
extern uint8_T CANNM_stRMSReq;         /* '<Root>/CANNM_stRMSReq' */

#endif                                 /* RTW_HEADER_AutoSarNm_private_h_ */

/*
 * File trailer for generated code.
 *
 * [EOF]
 */
