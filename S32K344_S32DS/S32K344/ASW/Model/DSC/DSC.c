/*
 * File: DSC.c
 *
 * Code generated for Simulink model 'DSC'.
 *
 * Model version                  : 14.409
 * Simulink Coder version         : 9.8 (R2022b) 13-May-2022
 * C/C++ source code generated on : Fri Nov  6 09:16:18 2026
 *
 * Target selection: ert.tlc
 * Embedded hardware selection: ARM Compatible->ARM 7
 * Code generation objectives: Unspecified
 * Validation result: Not run
 */

#include "DSC.h"
#include "rtwtypes.h"
#include "DSC_private.h"
#include "BSW_UART.h"

/* Exported block signals */
uint8_T ASW_stLEDR1Ctrl;               /* '<Root>/Switch1' */
uint8_T ASW_stLEDG1Ctrl;               /* '<Root>/Gain' */
uint8_T ASW_stLEDY1Ctrl;               /* '<Root>/Switch3' */
uint8_T ASW_stLEDB1Ctrl;               /* '<Root>/Switch5' */

/* Block states (default storage) */
DW_DSC_T DSC_DW;

/* Model step function */
void DSC_step(void)
{
  uint32_T rtb_Add2;

  /* Sum: '<Root>/Add' incorporates:
   *  Constant: '<Root>/Constant1'
   *  UnitDelay: '<Root>/Unit Delay'
   */
  rtb_Add2 = DSC_MODEL_CYCLE + DSC_DW.UnitDelay_DSTATE;

  /* Switch: '<Root>/Switch' incorporates:
   *  Switch: '<Root>/Switch1'
   */
  if (rtb_Add2 >= 1000U) {
    /* Update for UnitDelay: '<Root>/Unit Delay' incorporates:
     *  Constant: '<Root>/Constant'
     */
    DSC_DW.UnitDelay_DSTATE = 0U;

    /* Switch: '<Root>/Switch1' incorporates:
     *  DataTypeConversion: '<Root>/Data Type Conversion'
     *  Logic: '<Root>/Logical Operator'
     *  UnitDelay: '<Root>/Unit Delay1'
     */
    ASW_stLEDR1Ctrl = (uint8_T)(ASW_stLEDR1Ctrl == 0);
  } else {
    /* Update for UnitDelay: '<Root>/Unit Delay' */
    DSC_DW.UnitDelay_DSTATE = rtb_Add2;
  }

  /* End of Switch: '<Root>/Switch' */

  /* Gain: '<Root>/Gain' incorporates:
   *  Inport: '<Root>/Input'
   */
  ASW_stLEDG1Ctrl = ASP_stKL15;

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
