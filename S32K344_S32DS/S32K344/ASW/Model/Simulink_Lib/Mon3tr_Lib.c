/*
 * File: Mon3tr_Lib.c
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

#include "rtwtypes.h"
#include "Mon3tr_Lib.h"

/* Named constants for Chart: '<S2>/Chart' */
#define ASP_IN_Error                   ((uint8_T)1U)
#define ASP_IN_Error1                  ((uint8_T)2U)
#define ASP_IN_NoError                 ((uint8_T)3U)
#define ASP_IN_WaitError               ((uint8_T)4U)

/* Output and update for atomic system: '<S1>/Subsystem' */
void ErrDiag_DH(uint8_T rtu_ErrDiag, uint8_T rtu_ErrHeal, uint8_T rtu_ErrEnable,
                uint8_T rtu_ErrReset, uint32_T rtp_DiagDealTime, uint8_T
                rtp_DiagHealType, uint32_T rtp_DiagHealTime, uint32_T
                rtp_Model_Cycle, B_ErrDiag_DH_T *localB, DW_ErrDiag_DH_T
                *localDW)
{
  uint32_T qY;

  /* Chart: '<S2>/Chart' incorporates:
   *  Constant: '<S2>/Constant1'
   *  Constant: '<S2>/Constant2'
   *  Constant: '<S2>/Constant3'
   *  Constant: '<S2>/Constant4'
   *  RelationalOperator: '<S4>/FixPt Relational Operator'
   *  UnitDelay: '<S4>/Delay Input1'
   *
   * Block description for '<S4>/Delay Input1':
   *
   *  Store in Global RAM
   */
  if (localDW->is_active_c3_Mon3tr_Lib == 0U) {
    localDW->is_active_c3_Mon3tr_Lib = 1U;
    localDW->is_ALL = ASP_IN_NoError;

    /* :  tick = 0; */
    localDW->tick = 0U;

    /* :  Result = 0; */
    localB->Result = 0U;

    /* :  sf_internal_predicateOutput = ErrReset ~= 0 && DiagHealType ~= 1; */
  } else if ((rtu_ErrReset != localDW->DelayInput1_DSTATE) && (rtp_DiagHealType
              != 1)) {
    localDW->is_ALL = ASP_IN_NoError;

    /* :  tick = 0; */
    localDW->tick = 0U;

    /* :  Result = 0; */
    localB->Result = 0U;
  } else {
    switch (localDW->is_ALL) {
     case ASP_IN_Error:
      localB->Result = 1U;

      /* :  sf_internal_predicateOutput = ErrDiag ~= 0 && DiagHealType == 0; */
      if ((rtu_ErrDiag != 0) && (rtp_DiagHealType == 0)) {
        localDW->is_ALL = ASP_IN_Error1;
      }
      break;

     case ASP_IN_Error1:
      /* :  sf_internal_predicateOutput = ErrHeal == 0; */
      if (rtu_ErrHeal == 0) {
        localDW->is_ALL = ASP_IN_Error;

        /* :  tick = 0; */
        localDW->tick = 0U;

        /* :  Result = 1; */
        localB->Result = 1U;

        /* :  sf_internal_predicateOutput = tick >= DiagHealTime; */
      } else if (localDW->tick >= rtp_DiagHealTime) {
        localDW->is_ALL = ASP_IN_NoError;

        /* :  tick = 0; */
        localDW->tick = 0U;

        /* :  Result = 0; */
        localB->Result = 0U;
      } else {
        /* :  tick = tick + Model_Cycle; */
        qY = localDW->tick + /*MW:OvSatOk*/ rtp_Model_Cycle;
        if (qY < localDW->tick) {
          qY = MAX_uint32_T;
        }

        localDW->tick = qY;
      }
      break;

     case ASP_IN_NoError:
      localB->Result = 0U;

      /* :  sf_internal_predicateOutput = ErrDiag ~= 0 && ErrEnable ~= 0; */
      if ((rtu_ErrDiag != 0) && (rtu_ErrEnable != 0)) {
        localDW->is_ALL = ASP_IN_WaitError;
      }
      break;

     default:
      /* case IN_WaitError: */
      /* :  sf_internal_predicateOutput = ErrDiag == 0 || ErrEnable == 0; */
      if ((rtu_ErrDiag == 0) || (rtu_ErrEnable == 0)) {
        localDW->is_ALL = ASP_IN_NoError;

        /* :  tick = 0; */
        localDW->tick = 0U;

        /* :  Result = 0; */
        localB->Result = 0U;

        /* :  sf_internal_predicateOutput = tick >= DealTime; */
      } else if (localDW->tick >= rtp_DiagDealTime) {
        localDW->is_ALL = ASP_IN_Error;

        /* :  tick = 0; */
        localDW->tick = 0U;

        /* :  Result = 1; */
        localB->Result = 1U;
      } else {
        /* :  tick = tick + Model_Cycle; */
        qY = localDW->tick + /*MW:OvSatOk*/ rtp_Model_Cycle;
        if (qY < localDW->tick) {
          qY = MAX_uint32_T;
        }

        localDW->tick = qY;
      }
      break;
    }
  }

  /* End of Chart: '<S2>/Chart' */

  /* Update for UnitDelay: '<S4>/Delay Input1'
   *
   * Block description for '<S4>/Delay Input1':
   *
   *  Store in Global RAM
   */
  localDW->DelayInput1_DSTATE = rtu_ErrReset;
}

/* Output and update for atomic system: '<Root>/Subsystem1' */
void ErrDiag_IO(uint8_T rtu_ErrDiag, uint8_T rtu_ErrEnable, uint8_T rtu_ErrReset,
                uint32_T rtp_DiagDealTime, uint8_T rtp_DiagHealType, uint32_T
                rtp_DiagHealTime, uint32_T rtp_Model_Cycle, B_ErrDiag_IO_T
                *localB, DW_ErrDiag_IO_T *localDW)
{
  /* Outputs for Atomic SubSystem: '<S1>/Subsystem' */

  /* DataTypeConversion: '<S1>/Data Type Conversion' incorporates:
   *  Logic: '<S1>/Logical Operator'
   */
  ErrDiag_DH(rtu_ErrDiag, (uint8_T)(rtu_ErrDiag == 0), rtu_ErrEnable,
             rtu_ErrReset, rtp_DiagDealTime, rtp_DiagHealType, rtp_DiagHealTime,
             rtp_Model_Cycle, &localB->Subsystem, &localDW->Subsystem);

  /* End of Outputs for SubSystem: '<S1>/Subsystem' */
}

/*
 * File trailer for generated code.
 *
 * [EOF]
 */
