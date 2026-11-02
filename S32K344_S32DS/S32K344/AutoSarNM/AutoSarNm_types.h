/*
 * File: AutoSarNm_types.h
 *
 * Code generated for Simulink model 'AutoSarNm'.
 *
 * Model version                  : 14.367
 * Simulink Coder version         : 9.8 (R2022b) 13-May-2022
 * C/C++ source code generated on : Mon Nov  2 15:00:43 2026
 *
 * Target selection: ert.tlc
 * Embedded hardware selection: ARM Compatible->ARM 7
 * Code generation objectives: Unspecified
 * Validation result: Not run
 */

#ifndef RTW_HEADER_AutoSarNm_types_h_
#define RTW_HEADER_AutoSarNm_types_h_
#include "rtwtypes.h"

/* Includes for objects with custom storage classes */
#include "NMmain.h"
#ifndef DEFINED_TYPEDEF_FOR_CAN_FD_MESSAGE_BUS_
#define DEFINED_TYPEDEF_FOR_CAN_FD_MESSAGE_BUS_

typedef struct {
  uint8_T ProtocolMode;
  uint8_T Extended;
  uint8_T Length;
  uint8_T Remote;
  uint8_T Error;
  uint8_T BRS;
  uint8_T ESI;
  uint8_T DLC;
  uint32_T ID;
  uint32_T Reserved;
  real_T Timestamp;
  uint8_T Data[64];
} CAN_FD_MESSAGE_BUS;

#endif

#ifndef DEFINED_TYPEDEF_FOR_AutoSarNm_Sts_Enum_
#define DEFINED_TYPEDEF_FOR_AutoSarNm_Sts_Enum_

typedef enum {
  AutoSarNM_BSM = 0,                   /* Default value */
  AutoSarNM_PBSM = 1,
  AutoSarNM_RMS = 2,
  AutoSarNM_NOS = 3,
  AutoSarNM_RSS = 4,
  AutoSarNM_INVAILD = 255
} AutoSarNm_Sts_Enum;

#endif
#endif                                 /* RTW_HEADER_AutoSarNm_types_h_ */

/*
 * File trailer for generated code.
 *
 * [EOF]
 */
