/*
 * File: ASP.h
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

#ifndef RTW_HEADER_ASP_h_
#define RTW_HEADER_ASP_h_
#ifndef ASP_COMMON_INCLUDES_
#define ASP_COMMON_INCLUDES_
#include "rtwtypes.h"
#endif                                 /* ASP_COMMON_INCLUDES_ */

#include "ASP_types.h"
#include "Mon3tr_Lib.h"

/* Includes for objects with custom storage classes */
#include "ASP_Cfg.h"

/* Block signals (default storage) */
typedef struct {
  B_ErrDiag_IO_T Subsystem1;           /* '<Root>/Subsystem1' */
} B_ASP_T;

/* Block states (default storage) for system '<Root>' */
typedef struct {
  DW_ErrDiag_IO_T Subsystem1;          /* '<Root>/Subsystem1' */
} DW_ASP_T;

/* Block signals (default storage) */
extern B_ASP_T ASP_B;

/* Block states (default storage) */
extern DW_ASP_T ASP_DW;

/*
 * Exported Global Signals
 *
 * Note: Exported global signals are block signals with an exported global
 * storage class designation.  Code generation will declare the memory for
 * these signals and export their symbols.
 *
 */
extern uint8_T ASP_stKL15;             /* '<Root>/Subsystem1' */

/* Model entry point functions */
extern void ASP_initialize(void);
extern void ASP_step(void);

/*-
 * These blocks were eliminated from the model due to optimizations:
 *
 * Block '<Root>/Constant3' : Unused code path elimination
 * Block '<Root>/Constant4' : Unused code path elimination
 * Block '<S2>/Constant' : Unused code path elimination
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
 * '<Root>' : 'ASP'
 * '<S1>'   : 'ASP/Subsystem1'
 * '<S2>'   : 'ASP/Subsystem1/Subsystem'
 * '<S3>'   : 'ASP/Subsystem1/Subsystem/Chart'
 * '<S4>'   : 'ASP/Subsystem1/Subsystem/Detect Change'
 */
#endif                                 /* RTW_HEADER_ASP_h_ */

/*
 * File trailer for generated code.
 *
 * [EOF]
 */
