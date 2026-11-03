/*
 * File: AutoSarNm.h
 *
 * Code generated for Simulink model 'AutoSarNm'.
 *
 * Model version                  : 14.376
 * Simulink Coder version         : 9.8 (R2022b) 13-May-2022
 * C/C++ source code generated on : Tue Nov  3 08:36:09 2026
 *
 * Target selection: ert.tlc
 * Embedded hardware selection: ARM Compatible->ARM 7
 * Code generation objectives: Unspecified
 * Validation result: Not run
 */

#ifndef RTW_HEADER_AutoSarNm_h_
#define RTW_HEADER_AutoSarNm_h_
#ifndef AutoSarNm_COMMON_INCLUDES_
#define AutoSarNm_COMMON_INCLUDES_
#include <math.h>
#include "rtwtypes.h"
#include "can_fd_message.h"
#endif                                 /* AutoSarNm_COMMON_INCLUDES_ */

#include "AutoSarNm_types.h"

/* Definition for custom storage class: Define */
#define AUROSAR_NM_RDY2SLP_ENABLE      0U                        /* Referenced by:
                                                                  * '<Root>/From15'
                                                                  * '<Root>/Goto8'
                                                                  * '<Root>/Chart'
                                                                  * '<Root>/Constant29'
                                                                  */

/* Includes for objects with custom storage classes */
#include "NMmain.h"

/* Exported data define */

/* Definition for custom storage class: Define */
#define AUROSAR_NM_MODEL_RUN_STEP      10U                       /* Referenced by: '<Root>/Constant5' */

/* user code (top of header file) */
#include "NMmain.h"

/* Block signals (default storage) */
typedef struct {
  CAN_FD_MESSAGE_BUS CAN_msgAutoSarNmFrame_i;/* '<Root>/CAN FD Pack' */
  CAN_FD_MESSAGE_BUS CANFDPack;        /* '<S9>/CAN FD Pack' */
  uint8_T Compare;                     /* '<S3>/Compare' */
  uint8_T VariantMerge_For_Variant_Source;
  uint8_T DataTypeConversion6;         /* '<Root>/Data Type Conversion6' */
  uint8_T NmRdy2Slp;                   /* '<Root>/Chart' */
} B_AutoSarNm_T;

/* Block states (default storage) for system '<Root>' */
typedef struct {
  uint32_T DelayInput1_DSTATE;         /* '<S7>/Delay Input1' */
  uint32_T UnitDelay_DSTATE;           /* '<Root>/Unit Delay' */
  uint32_T RMSSendFrameCntDelay;       /* '<Root>/Chart' */
  uint32_T temporalCounter_i1;         /* '<Root>/Chart' */
  int_T CANFDPack_ModeSignalID;        /* '<Root>/CAN FD Pack' */
  int_T CANFDPack_ModeSignalID_a;      /* '<S9>/CAN FD Pack' */
  uint8_T DelayInput1_DSTATE_h;        /* '<S5>/Delay Input1' */
  uint8_T DelayInput1_DSTATE_d;        /* '<S6>/Delay Input1' */
  uint8_T is_c3_AutoSarNm;             /* '<Root>/Chart' */
  uint8_T is_BSM;                      /* '<Root>/Chart' */
  uint8_T is_ALL;                      /* '<Root>/Chart' */
  uint8_T is_Set_AppSendMode;          /* '<Root>/Chart' */
  uint8_T is_Set_NmSendMode;           /* '<Root>/Chart' */
  uint8_T is_SendLastFrame;            /* '<Root>/Chart' */
  uint8_T App_SendFlg;                 /* '<Root>/Chart' */
  uint8_T is_active_c3_AutoSarNm;      /* '<Root>/Chart' */
} DW_AutoSarNm_T;

/* Invariant block signals (default storage) */
typedef struct {
  const uint16_T ShiftArithmetic10;    /* '<Root>/Shift Arithmetic10' */
  const uint16_T ShiftArithmetic11;    /* '<Root>/Shift Arithmetic11' */
  const uint16_T ShiftArithmetic12;    /* '<Root>/Shift Arithmetic12' */
  const uint16_T ShiftArithmetic13;    /* '<Root>/Shift Arithmetic13' */
  const uint16_T ShiftArithmetic14;    /* '<Root>/Shift Arithmetic14' */
  const uint16_T ShiftArithmetic15;    /* '<Root>/Shift Arithmetic15' */
  const uint16_T ShiftArithmetic2;     /* '<Root>/Shift Arithmetic2' */
  const uint16_T ShiftArithmetic3;     /* '<Root>/Shift Arithmetic3' */
  const uint16_T ShiftArithmetic4;     /* '<Root>/Shift Arithmetic4' */
  const uint16_T ShiftArithmetic5;     /* '<Root>/Shift Arithmetic5' */
  const uint16_T ShiftArithmetic6;     /* '<Root>/Shift Arithmetic6' */
  const uint16_T ShiftArithmetic7;     /* '<Root>/Shift Arithmetic7' */
  const uint16_T ShiftArithmetic8;     /* '<Root>/Shift Arithmetic8' */
  const uint16_T ShiftArithmetic9;     /* '<Root>/Shift Arithmetic9' */
} ConstB_AutoSarNm_T;

/* Constant parameters (default storage) */
typedef struct {
  /* Pooled Parameter (Mixed Expressions)
   * Referenced by:
   *   '<Root>/Constant10'
   *   '<Root>/Constant11'
   *   '<Root>/Constant12'
   *   '<Root>/Constant14'
   *   '<Root>/Constant15'
   *   '<Root>/Constant16'
   *   '<Root>/Constant17'
   *   '<Root>/Constant18'
   *   '<Root>/Constant19'
   *   '<Root>/Constant20'
   *   '<Root>/Constant29'
   *   '<Root>/Constant9'
   *   '<S5>/Delay Input1'
   *   '<S6>/Delay Input1'
   *   '<S9>/Constant1'
   *   '<S10>/Constant'
   */
  uint8_T pooled2;
} ConstP_AutoSarNm_T;

/* Block signals (default storage) */
extern B_AutoSarNm_T AutoSarNm_B;

/* Block states (default storage) */
extern DW_AutoSarNm_T AutoSarNm_DW;
extern const ConstB_AutoSarNm_T AutoSarNm_ConstB;/* constant block i/o */

/* Constant parameters (default storage) */
extern const ConstP_AutoSarNm_T AutoSarNm_ConstP;

/*
 * Exported Global Signals
 *
 * Note: Exported global signals are block signals with an exported global
 * storage class designation.  Code generation will declare the memory for
 * these signals and export their symbols.
 *
 */
extern AutoSarNm_Sts_Enum AutoSarNM_sts;/* '<Root>/Data Type Conversion1' */

/*
 * Exported States
 *
 * Note: Exported states are block states with an exported global
 * storage class designation.  Code generation will declare the memory for these
 * states and exports their symbols.
 *
 */
extern CAN_FD_MESSAGE_BUS CAN_msgAutoSarNmFrame;
                            /* Simulink.Signal object 'CAN_msgAutoSarNmFrame' */

/* Model entry point functions */
extern void AutoSarNm_initialize(void);
extern void AutoSarNm_step(void);

/*-
 * These blocks were eliminated from the model due to optimizations:
 *
 * Block '<Root>/Constant' : Unused code path elimination
 * Block '<Root>/Constant1' : Unused code path elimination
 * Block '<Root>/Constant13' : Unused code path elimination
 * Block '<Root>/Constant2' : Unused code path elimination
 * Block '<Root>/Constant21' : Unused code path elimination
 * Block '<Root>/Constant22' : Unused code path elimination
 * Block '<Root>/Constant23' : Unused code path elimination
 * Block '<Root>/Constant24' : Unused code path elimination
 * Block '<Root>/Constant25' : Unused code path elimination
 * Block '<Root>/Constant26' : Unused code path elimination
 * Block '<Root>/Constant27' : Unused code path elimination
 * Block '<Root>/Constant28' : Unused code path elimination
 * Block '<Root>/Constant3' : Unused code path elimination
 * Block '<Root>/Constant7' : Unused code path elimination
 * Block '<S4>/Delay Input1' : Unused code path elimination
 * Block '<S4>/FixPt Relational Operator' : Unused code path elimination
 * Block '<Root>/Data Type Conversion' : Eliminate redundant data type conversion
 * Block '<Root>/Shift Arithmetic' : Eliminated trivial shift
 */

/*-
 * The generated code includes comments that allow you to trace directly
 * back to the appropriate location in the model.  The basic format
 * is <system>/block_name, where system is the system number (uniquely
 * assigned by Simulink) and block_name is the name of the block.
 *
 * Use the MATLAB hilite_system command to trace the generated code back
 * to the model.  For example,
 *
 * hilite_system('<S3>')    - opens system 3
 * hilite_system('<S3>/Kp') - opens and selects block Kp which resides in S3
 *
 * Here is the system hierarchy for this model
 *
 * '<Root>' : 'AutoSarNm'
 * '<S1>'   : 'AutoSarNm/Chart'
 * '<S2>'   : 'AutoSarNm/Compare To Constant'
 * '<S3>'   : 'AutoSarNm/Compare To Constant1'
 * '<S4>'   : 'AutoSarNm/Detect Change'
 * '<S5>'   : 'AutoSarNm/Detect Change1'
 * '<S6>'   : 'AutoSarNm/Detect Change2'
 * '<S7>'   : 'AutoSarNm/Detect Change3'
 * '<S8>'   : 'AutoSarNm/Initialize Function'
 * '<S9>'   : 'AutoSarNm/Initialize Function/Atomic Subsystem'
 * '<S10>'  : 'AutoSarNm/Initialize Function/Atomic Subsystem1'
 * '<S11>'  : 'AutoSarNm/Initialize Function/Atomic Subsystem2'
 */
#endif                                 /* RTW_HEADER_AutoSarNm_h_ */

/*
 * File trailer for generated code.
 *
 * [EOF]
 */
