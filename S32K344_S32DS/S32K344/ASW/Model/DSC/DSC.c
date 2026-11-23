/*
 * File: DSC.c
 *
 * Code generated for Simulink model 'DSC'.
 *
 * Model version                  : 14.414
 * Simulink Coder version         : 9.8 (R2022b) 13-May-2022
 * C/C++ source code generated on : Mon Nov 23 09:12:31 2026
 *
 * Target selection: ert.tlc
 * Embedded hardware selection: ARM Compatible->ARM 7
 * Code generation objectives: Unspecified
 * Validation result: Not run
 */

#include "DSC.h"
#include "Mon3tr_Lib.h"
#include "rtwtypes.h"
#include "DSC_private.h"
#include "BSW_UART.h"

/* Exported block signals */
uint8_T ASW_stLEDG1Ctrl;               /* '<Root>/Gain' */
uint8_T ASW_stLEDR1Ctrl;               /* '<Root>/Subsystem1' */
uint8_T ASW_stLEDY1Ctrl;               /* '<Root>/Switch3' */
uint8_T ASW_stLEDB1Ctrl;               /* '<Root>/Switch5' */

/* Block signals (default storage) */
B_DSC_T DSC_B;

/* Block states (default storage) */
DW_DSC_T DSC_DW;

/* Model step function */
void DSC_step(void)
{
  /* Gain: '<Root>/Gain' incorporates:
   *  Inport: '<Root>/Input'
   */
  ASW_stLEDG1Ctrl = ASP_stKL15;

  /* Outputs for Atomic SubSystem: '<Root>/Subsystem1' */
  /* RelationalOperator: '<S1>/Compare' incorporates:
   *  Constant: '<Root>/Constant4'
   *  Constant: '<Root>/Constant5'
   *  Constant: '<S1>/Constant'
   *  UnitDelay: '<Root>/Unit Delay6'
   */
  ErrDiag_IO((uint8_T)(ASW_stLEDR1Ctrl == 0), 1, 0, 1000U, DSC_MODEL_CYCLE, 0,
             1000U, &DSC_B.Subsystem1, &DSC_DW.Subsystem1);

  /* End of Outputs for SubSystem: '<Root>/Subsystem1' */

  /* SignalConversion generated from: '<Root>/Subsystem1' */
  ASW_stLEDR1Ctrl = DSC_B.Subsystem1.Subsystem.Result;

  /* Switch: '<Root>/Switch3' incorporates:
   *  Inport: '<Root>/BSW_u32UART6RxFrameCnt'
   *  Sum: '<Root>/Add1'
   *  Switch: '<Root>/Switch2'
   *  UnitDelay: '<Root>/Unit Delay2'
   */
  if (BSW_u32UART6RxFrameCnt - DSC_DW.UnitDelay2_DSTATE > 10U) {
    /* Switch: '<Root>/Switch3' incorporates:
     *  DataTypeConversion: '<Root>/Data Type Conversion1'
     *  Logic: '<Root>/Logical Operator1'
     *  UnitDelay: '<Root>/Unit Delay3'
     */
    ASW_stLEDY1Ctrl = (uint8_T)(ASW_stLEDY1Ctrl == 0);

    /* Update for UnitDelay: '<Root>/Unit Delay2' */
    DSC_DW.UnitDelay2_DSTATE = BSW_u32UART6RxFrameCnt;
  }

  /* End of Switch: '<Root>/Switch3' */

  /* Switch: '<Root>/Switch5' incorporates:
   *  Inport: '<Root>/BSW_cntCan0RxCnt'
   *  Sum: '<Root>/Add2'
   *  Switch: '<Root>/Switch4'
   *  UnitDelay: '<Root>/Unit Delay4'
   */
  if (BSW_cntCan0RxCnt - DSC_DW.UnitDelay4_DSTATE > 100U) {
    /* Switch: '<Root>/Switch5' incorporates:
     *  DataTypeConversion: '<Root>/Data Type Conversion2'
     *  Logic: '<Root>/Logical Operator2'
     *  UnitDelay: '<Root>/Unit Delay5'
     */
    ASW_stLEDB1Ctrl = (uint8_T)(ASW_stLEDB1Ctrl == 0);

    /* Update for UnitDelay: '<Root>/Unit Delay4' */
    DSC_DW.UnitDelay4_DSTATE = BSW_cntCan0RxCnt;
  }

  /* End of Switch: '<Root>/Switch5' */
}

/* Model initialize function */
void DSC_initialize(void)
{
  /* (no initialization code required) */
}

/*
 * File trailer for generated code.
 *
 * [EOF]
 */
