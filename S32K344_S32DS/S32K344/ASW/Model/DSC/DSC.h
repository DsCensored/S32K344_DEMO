/*
 * File: DSC.h
 *
 * Code generated for Simulink model 'DSC'.
 *
 * Model version                  : 14.414
 * Simulink Coder version         : 9.8 (R2022b) 13-May-2022
 * C/C++ source code generated on : Wed Nov 25 09:28:05 2026
 *
 * Target selection: ert.tlc
 * Embedded hardware selection: ARM Compatible->ARM 7
 * Code generation objectives: Unspecified
 * Validation result: Not run
 */

#ifndef RTW_HEADER_DSC_h_
#define RTW_HEADER_DSC_h_
#ifndef DSC_COMMON_INCLUDES_
#define DSC_COMMON_INCLUDES_
#include "rtwtypes.h"
#endif                                 /* DSC_COMMON_INCLUDES_ */

#include "DSC_types.h"
#include "Mon3tr_Lib.h"

/* Includes for objects with custom storage classes */
#include "BSW_UART.h"

/* Exported data define */

/* Definition for custom storage class: Define */
#define DSC_MODEL_CYCLE                50U                       /* Referenced by: '<Root>/Subsystem1' */

/* Block signals (default storage) */
typedef struct {
  B_ErrDiag_IO_T Subsystem1;           /* '<Root>/Subsystem1' */
} B_DSC_T;

/* Block states (default storage) for system '<Root>' */
typedef struct {
  uint32_T UnitDelay2_DSTATE;          /* '<Root>/Unit Delay2' */
  uint32_T UnitDelay4_DSTATE;          /* '<Root>/Unit Delay4' */
  DW_ErrDiag_IO_T Subsystem1;          /* '<Root>/Subsystem1' */
} DW_DSC_T;

/* Block signals (default storage) */
extern B_DSC_T DSC_B;

/* Block states (default storage) */
extern DW_DSC_T DSC_DW;

/*
 * Exported Global Signals
 *
 * Note: Exported global signals are block signals with an exported global
 * storage class designation.  Code generation will declare the memory for
 * these signals and export their symbols.
 *
 */
extern uint8_T ASW_stLEDG1Ctrl;        /* '<Root>/Gain' */
extern uint8_T ASW_stLEDR1Ctrl;        /* '<Root>/Subsystem1' */
extern uint8_T ASW_stLEDY1Ctrl;        /* '<Root>/Switch3' */
extern uint8_T ASW_stLEDB1Ctrl;        /* '<Root>/Switch5' */

/* Model entry point functions */
extern void DSC_initialize(void);
extern void DSC_step(void);

/*-
 * These blocks were eliminated from the model due to optimizations:
 *
 * Block '<Root>/Constant3' : Unused code path elimination
 * Block '<Root>/Data Type Conversion4' : Eliminate redundant data type conversion
 * Block '<Root>/Data Type Conversion5' : Eliminate redundant data type conversion
 * Block '<S3>/Constant' : Unused code path elimination
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
 * '<Root>' : 'DSC'
 * '<S1>'   : 'DSC/Compare To Constant'
 * '<S2>'   : 'DSC/Subsystem1'
 * '<S3>'   : 'DSC/Subsystem1/Subsystem'
 * '<S4>'   : 'DSC/Subsystem1/Subsystem/Chart'
 * '<S5>'   : 'DSC/Subsystem1/Subsystem/Detect Change'
 */
#endif                                 /* RTW_HEADER_DSC_h_ */

/*
 * File trailer for generated code.
 *
 * [EOF]
 */
