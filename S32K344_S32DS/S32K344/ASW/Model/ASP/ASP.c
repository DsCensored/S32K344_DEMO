/*
 * File: ASP.c
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

#include "ASP.h"
#include "Mon3tr_Lib.h"
#include "rtwtypes.h"
#include "ASP_private.h"
#include "ASP_Cfg.h"

/* Exported block signals */
uint8_T ASP_stKL15;                    /* '<Root>/Subsystem1' */

/* Block signals (default storage) */
B_ASP_T ASP_B;

/* Block states (default storage) */
DW_ASP_T ASP_DW;

/* Model step function */
void ASP_step(void)
{
  /* Outputs for Atomic SubSystem: '<Root>/Subsystem1' */

  /* Inport: '<Root>/Input' incorporates:
   *  Constant: '<Root>/Constant'
   *  Constant: '<Root>/Constant1'
   */
  ErrDiag_IO(BSW_stKL15, 1, 0, ASP_HW_DEB_TIME, 0, ASP_HW_DEB_TIME,
             ASP_MODEL_CYCLE, &ASP_B.Subsystem1, &ASP_DW.Subsystem1);

  /* End of Outputs for SubSystem: '<Root>/Subsystem1' */

  /* SignalConversion generated from: '<Root>/Subsystem1' */
  ASP_stKL15 = ASP_B.Subsystem1.Subsystem.Result;
}

/* Model initialize function */
void ASP_initialize(void)
{
  /* (no initialization code required) */
}

/*
 * File trailer for generated code.
 *
 * [EOF]
 */
