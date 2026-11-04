/*
 * File: Mon3tr_Lib.h
 *
 * Code generated for Simulink model 'ASP'.
 *
 * Model version                  : 14.387
 * Simulink Coder version         : 9.8 (R2022b) 13-May-2022
 * C/C++ source code generated on : Wed Nov  4 10:57:55 2026
 *
 * Target selection: ert.tlc
 * Embedded hardware selection: ARM Compatible->ARM 7
 * Code generation objectives: Unspecified
 * Validation result: Not run
 */

#ifndef RTW_HEADER_Mon3tr_Lib_h_
#define RTW_HEADER_Mon3tr_Lib_h_
#ifndef ASP_COMMON_INCLUDES_
#define ASP_COMMON_INCLUDES_
#include "rtwtypes.h"
#endif                                 /* ASP_COMMON_INCLUDES_ */

/* Block signals for system '<S1>/Subsystem' */
typedef struct {
  uint8_T Result;                      /* '<S2>/Chart' */
} B_ErrDiag_DH_T;

/* Block states (default storage) for system '<S1>/Subsystem' */
typedef struct {
  uint32_T tick;                       /* '<S2>/Chart' */
  uint8_T DelayInput1_DSTATE;          /* '<S4>/Delay Input1' */
  uint8_T is_ALL;                      /* '<S2>/Chart' */
  uint8_T is_active_c3_Mon3tr_Lib;     /* '<S2>/Chart' */
} DW_ErrDiag_DH_T;

/* Block signals for system '<Root>/Subsystem1' */
typedef struct {
  B_ErrDiag_DH_T Subsystem;            /* '<S1>/Subsystem' */
} B_ErrDiag_IO_T;

/* Block states (default storage) for system '<Root>/Subsystem1' */
typedef struct {
  DW_ErrDiag_DH_T Subsystem;           /* '<S1>/Subsystem' */
} DW_ErrDiag_IO_T;

extern void ErrDiag_DH(uint8_T rtu_ErrDiag, uint8_T rtu_ErrHeal, uint8_T
  rtu_ErrEnable, uint8_T rtu_ErrReset, uint32_T rtp_DiagDealTime, uint8_T
  rtp_DiagHealType, uint32_T rtp_DiagHealTime, uint32_T rtp_Model_Cycle,
  B_ErrDiag_DH_T *localB, DW_ErrDiag_DH_T *localDW);
extern void ErrDiag_IO(uint8_T rtu_ErrDiag, uint8_T rtu_ErrEnable, uint8_T
  rtu_ErrReset, uint32_T rtp_DiagDealTime, uint8_T rtp_DiagHealType, uint32_T
  rtp_DiagHealTime, uint32_T rtp_Model_Cycle, B_ErrDiag_IO_T *localB,
  DW_ErrDiag_IO_T *localDW);

#endif                                 /* RTW_HEADER_Mon3tr_Lib_h_ */

/*
 * File trailer for generated code.
 *
 * [EOF]
 */
