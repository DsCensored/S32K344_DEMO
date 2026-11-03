/*
 * File: AutoSarNm.c
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

#include "AutoSarNm.h"
#include "AutoSarNm_types.h"
#include "rtwtypes.h"
#include "AutoSarNm_private.h"
#include "NMmain.h"

/* Named constants for Chart: '<Root>/Chart' */
#define AutoSarNm_IN_ALL               ((uint8_T)1U)
#define AutoSarNm_IN_BSM               ((uint8_T)2U)
#define AutoSarNm_IN_Fast              ((uint8_T)1U)
#define AutoSarNm_IN_NOS               ((uint8_T)1U)
#define AutoSarNm_IN_NO_ACTIVE_CHILD   ((uint8_T)0U)
#define AutoSarNm_IN_PBSM              ((uint8_T)3U)
#define AutoSarNm_IN_RMS               ((uint8_T)2U)
#define AutoSarNm_IN_RSS               ((uint8_T)3U)
#define AutoSarNm_IN_Slow              ((uint8_T)2U)
#define AutoSarNm_IN_end               ((uint8_T)1U)
#define AutoSarNm_IN_wait              ((uint8_T)1U)
#define AutoSarNm_IN_wait1             ((uint8_T)2U)
#define AutoSarNm_IN_wait_d            ((uint8_T)2U)

/* Exported block signals */
AutoSarNm_Sts_Enum AutoSarNM_sts;      /* '<Root>/Data Type Conversion1' */

/* Exported block states */
CAN_FD_MESSAGE_BUS CAN_msgAutoSarNmFrame;
                            /* Simulink.Signal object 'CAN_msgAutoSarNmFrame' */

/* Block signals (default storage) */
B_AutoSarNm_T AutoSarNm_B;

/* Block states (default storage) */
DW_AutoSarNm_T AutoSarNm_DW;

/* Forward declaration for local functions */
static void AutoSarNm_enter_internal_RMS(const uint8_T
  *FixPtRelationalOperator_p);
static void AutoSarNm_exit_internal_ALL(void);
static void AutoSarNm_enter_internal_RSS(void);

/* Function for Chart: '<Root>/Chart' */
static void AutoSarNm_enter_internal_RMS(const uint8_T
  *FixPtRelationalOperator_p)
{
  /* :  sf_internal_predicateOutput = App_SendFlg == 0; */
  if (AutoSarNm_DW.App_SendFlg == 0) {
    AutoSarNm_DW.is_Set_AppSendMode = AutoSarNm_IN_Fast;
  } else {
    AutoSarNm_DW.is_Set_AppSendMode = AutoSarNm_IN_Slow;

    /* :  NM_SetAppMsgEnable(uint8(1)); */
    NM_SetAppMsgEnable(1U);

    /* :  App_SendFlg = 1; */
    AutoSarNm_DW.App_SendFlg = 1U;
  }

  /* Inport: '<Root>/AutoSarNM_stWKUPMode' */
  /* :  sf_internal_predicateOutput = AutoSarNM_stWKUPMode == 0 || CANNM_stRMSReq ~= 0; */
  if ((AutoSarNM_stWKUPMode == 0) || (*FixPtRelationalOperator_p != 0)) {
    AutoSarNm_DW.is_Set_NmSendMode = AutoSarNm_IN_Fast;

    /* :  NM_SetNmMsgCycle(CANNM_IMMEDIATE_CYCLE_TIME); */
    NM_SetNmMsgCycle(CANNM_IMMEDIATE_CYCLE_TIME);

    /* :  NM_SetNmMsgEnable(uint8(1)); */
    NM_SetNmMsgEnable(1U);

    /* :  NM_ResetCanNmSendCnt(); */
    NM_ResetCanNmSendCnt();

    /* Inport: '<Root>/CANNM_cntAuroSarNMFrameSendCnt' */
    /* :  RMSSendFrameCntDelay = CANNM_cntAuroSarNMFrameSendCnt; */
    AutoSarNm_DW.RMSSendFrameCntDelay = CANNM_cntAuroSarNMFrameSendCnt;
  } else {
    AutoSarNm_DW.is_Set_NmSendMode = AutoSarNm_IN_Slow;

    /* :  NM_SetNmMsgEnable(uint8(1)); */
    NM_SetNmMsgEnable(1U);

    /* :  NM_SetNmMsgCycle(CANNM_MSG_CYCLE_TIME); */
    NM_SetNmMsgCycle(CANNM_MSG_CYCLE_TIME);
  }

  /* End of Inport: '<Root>/AutoSarNM_stWKUPMode' */
}

/* Function for Chart: '<Root>/Chart' */
static void AutoSarNm_exit_internal_ALL(void)
{
  AutoSarNm_DW.is_Set_NmSendMode = AutoSarNm_IN_NO_ACTIVE_CHILD;
  AutoSarNm_DW.is_Set_AppSendMode = AutoSarNm_IN_NO_ACTIVE_CHILD;
  AutoSarNm_DW.is_SendLastFrame = AutoSarNm_IN_NO_ACTIVE_CHILD;
  AutoSarNm_DW.is_ALL = AutoSarNm_IN_NO_ACTIVE_CHILD;
}

/* Function for Chart: '<Root>/Chart' */
static void AutoSarNm_enter_internal_RSS(void)
{
  /* :  sf_internal_predicateOutput = AUROSAR_NM_RDY2SLP_ENABLE ~= 0; */
  if (AUROSAR_NM_RDY2SLP_ENABLE != 0U) {
    AutoSarNm_DW.is_SendLastFrame = AutoSarNm_IN_wait_d;

    /* :  NmRdy2Slp = 1; */
    AutoSarNm_B.NmRdy2Slp = 1U;

    /* Inport: '<Root>/CANNM_cntAuroSarNMFrameSendCnt' */
    /* :  RMSSendFrameCntDelay = CANNM_cntAuroSarNMFrameSendCnt; */
    AutoSarNm_DW.RMSSendFrameCntDelay = CANNM_cntAuroSarNMFrameSendCnt;
  } else {
    AutoSarNm_DW.is_SendLastFrame = AutoSarNm_IN_end;

    /* :  NmRdy2Slp = 0; */
    AutoSarNm_B.NmRdy2Slp = 0U;

    /* :  NM_SetNmMsgEnable(uint8(0)); */
    NM_SetNmMsgEnable(0U);
  }

  AutoSarNm_DW.temporalCounter_i1 = 0U;
}

/* Model step function */
void AutoSarNm_step(void)
{
  int32_T u0;
  uint32_T qY;
  uint32_T rtb_Switch;
  uint16_T BitwiseOR;
  uint8_T AutoSarNM_sts_a;
  uint8_T FixPtRelationalOperator;
  uint8_T FixPtRelationalOperator_p;

  /* RelationalOperator: '<S3>/Compare' incorporates:
   *  Constant: '<S3>/Constant'
   *  Inport: '<Root>/AutoSarNM_stWKUPMode'
   */
  AutoSarNm_B.Compare = (uint8_T)(AutoSarNM_stWKUPMode == 1);

  /* RelationalOperator: '<S7>/FixPt Relational Operator' incorporates:
   *  Inport: '<Root>/CANNM_cntAuroSarNMFrameSendCnt'
   *  UnitDelay: '<S7>/Delay Input1'
   *
   * Block description for '<S7>/Delay Input1':
   *
   *  Store in Global RAM
   */
  FixPtRelationalOperator = (uint8_T)(CANNM_cntAuroSarNMFrameSendCnt !=
    AutoSarNm_DW.DelayInput1_DSTATE);

  /* Switch: '<Root>/Switch' incorporates:
   *  Constant: '<Root>/Constant4'
   *  Constant: '<Root>/Constant5'
   *  Inport: '<Root>/AutoSarNM_cntUDSMsgRxCnt'
   *  RelationalOperator: '<S5>/FixPt Relational Operator'
   *  UnitDelay: '<S5>/Delay Input1'
   *
   * Block description for '<S5>/Delay Input1':
   *
   *  Store in Global RAM
   */
  if (AutoSarNM_cntUDSMsgRxCnt != AutoSarNm_DW.DelayInput1_DSTATE_h) {
    rtb_Switch = AUROSAR_NM_UDS_OVERTIME;
  } else {
    qY = AUROSAR_NM_MODEL_RUN_STEP;
    if (AUROSAR_NM_MODEL_RUN_STEP > 2147483647U) {
      qY = 2147483647U;
    }

    /* Saturate: '<Root>/Saturation' incorporates:
     *  Constant: '<Root>/Constant5'
     */
    rtb_Switch = AUROSAR_NM_UDS_OVERTIME;
    if (AUROSAR_NM_UDS_OVERTIME > 2147483647U) {
      rtb_Switch = 2147483647U;
    }

    /* Sum: '<Root>/Add' incorporates:
     *  Constant: '<Root>/Constant5'
     *  DataTypeConversion: '<Root>/Data Type Conversion4'
     *  UnitDelay: '<Root>/Unit Delay'
     */
    u0 = (int32_T)AutoSarNm_DW.UnitDelay_DSTATE - (int32_T)qY;

    /* Saturate: '<Root>/Saturation' incorporates:
     *  DataTypeConversion: '<Root>/Data Type Conversion5'
     */
    if (u0 <= (int32_T)rtb_Switch) {
      if (u0 < 0) {
        rtb_Switch = 0U;
      } else {
        rtb_Switch = (uint32_T)u0;
      }
    }
  }

  /* End of Switch: '<Root>/Switch' */

  /* S-Function (sfix_bitop): '<Root>/Bitwise OR' incorporates:
   *  ArithShift: '<Root>/Shift Arithmetic1'
   *  Constant: '<S2>/Constant'
   *  Inport: '<Root>/BSW_stKL15'
   *  RelationalOperator: '<S2>/Compare'
   */
  BitwiseOR = (uint16_T)((rtb_Switch != 0U) | BSW_stKL15 << 1 |
    AutoSarNm_ConstB.ShiftArithmetic2 | AutoSarNm_ConstB.ShiftArithmetic3 |
    AutoSarNm_ConstB.ShiftArithmetic4 | AutoSarNm_ConstB.ShiftArithmetic5 |
    AutoSarNm_ConstB.ShiftArithmetic6 | AutoSarNm_ConstB.ShiftArithmetic7 |
    AutoSarNm_ConstB.ShiftArithmetic8 | AutoSarNm_ConstB.ShiftArithmetic9 |
    AutoSarNm_ConstB.ShiftArithmetic10 | AutoSarNm_ConstB.ShiftArithmetic11 |
    AutoSarNm_ConstB.ShiftArithmetic12 | AutoSarNm_ConstB.ShiftArithmetic13 |
    AutoSarNm_ConstB.ShiftArithmetic14 | AutoSarNm_ConstB.ShiftArithmetic15);

  /* RelationalOperator: '<S6>/FixPt Relational Operator' incorporates:
   *  Inport: '<Root>/CANNM_stRMSReq'
   *  UnitDelay: '<S6>/Delay Input1'
   *
   * Block description for '<S6>/Delay Input1':
   *
   *  Store in Global RAM
   */
  FixPtRelationalOperator_p = (uint8_T)(CANNM_stRMSReq !=
    AutoSarNm_DW.DelayInput1_DSTATE_d);

  /* Chart: '<Root>/Chart' incorporates:
   *  Inport: '<Root>/AutoSarNM_stWKUPMode'
   *  Inport: '<Root>/CANNM_cntAuroSarNMFrameSendCnt'
   */
  if (AutoSarNm_DW.temporalCounter_i1 < 536870911U) {
    AutoSarNm_DW.temporalCounter_i1++;
  }

  if (AutoSarNm_DW.is_active_c3_AutoSarNm == 0U) {
    AutoSarNm_DW.is_active_c3_AutoSarNm = 1U;
    AutoSarNm_DW.is_c3_AutoSarNm = AutoSarNm_IN_BSM;

    /* :  NM_SetNmMsgEnable(uint8(0)); */
    NM_SetNmMsgEnable(0U);

    /* :  NM_SetAppMsgEnable(uint8(0)); */
    NM_SetAppMsgEnable(0U);

    /* :  NM_ResetCanNmSendCnt(); */
    NM_ResetCanNmSendCnt();

    /* :  App_SendFlg = 0; */
    AutoSarNm_DW.App_SendFlg = 0U;

    /* :  AutoSarNM_sts = uint8(0); */
    AutoSarNM_sts_a = 0U;

    /* :  NmRdy2Slp = 0; */
    AutoSarNm_B.NmRdy2Slp = 0U;

    /* :  NM_PbsmGetWkupMode(uint8(1)); */
    NM_PbsmGetWkupMode(1U);
    AutoSarNm_DW.is_BSM = AutoSarNm_IN_wait;
    AutoSarNm_DW.temporalCounter_i1 = 0U;
  } else {
    switch (AutoSarNm_DW.is_c3_AutoSarNm) {
     case AutoSarNm_IN_ALL:
      /* :  sf_internal_predicateOutput = CANNM_stRMSReq ~= 0; */
      if (FixPtRelationalOperator_p != 0) {
        AutoSarNm_exit_internal_ALL();
        AutoSarNm_DW.is_c3_AutoSarNm = AutoSarNm_IN_ALL;
        AutoSarNm_DW.is_ALL = AutoSarNm_IN_RMS;
        AutoSarNm_DW.temporalCounter_i1 = 0U;

        /* :  AutoSarNM_sts = uint8(2); */
        AutoSarNM_sts_a = 2U;

        /* 1、上电后首帧必须是网管管理帧 */
        /* 2、被动唤醒不进入快发模式 */
        /* :  sf_internal_predicateOutput = App_SendFlg == 0; */
        if (AutoSarNm_DW.App_SendFlg == 0) {
          AutoSarNm_DW.is_Set_AppSendMode = AutoSarNm_IN_Fast;
        } else {
          AutoSarNm_DW.is_Set_AppSendMode = AutoSarNm_IN_Slow;

          /* :  NM_SetAppMsgEnable(uint8(1)); */
          NM_SetAppMsgEnable(1U);

          /* :  App_SendFlg = 1; */
          AutoSarNm_DW.App_SendFlg = 1U;
        }

        AutoSarNm_DW.is_Set_NmSendMode = AutoSarNm_IN_Fast;

        /* :  NM_SetNmMsgCycle(CANNM_IMMEDIATE_CYCLE_TIME); */
        NM_SetNmMsgCycle(CANNM_IMMEDIATE_CYCLE_TIME);

        /* :  NM_SetNmMsgEnable(uint8(1)); */
        NM_SetNmMsgEnable(1U);

        /* :  NM_ResetCanNmSendCnt(); */
        NM_ResetCanNmSendCnt();

        /* :  RMSSendFrameCntDelay = CANNM_cntAuroSarNMFrameSendCnt; */
        AutoSarNm_DW.RMSSendFrameCntDelay = CANNM_cntAuroSarNMFrameSendCnt;
      } else {
        switch (AutoSarNm_DW.is_ALL) {
         case AutoSarNm_IN_NOS:
          AutoSarNM_sts_a = 3U;

          /* :  sf_internal_predicateOutput = AutoSarNmKeepSrc == 0; */
          if (BitwiseOR == 0) {
            AutoSarNm_DW.is_ALL = AutoSarNm_IN_RSS;

            /* :  AutoSarNM_sts = uint8(4); */
            AutoSarNM_sts_a = 4U;
            AutoSarNm_enter_internal_RSS();
          }
          break;

         case AutoSarNm_IN_RMS:
          AutoSarNM_sts_a = 2U;

          /* :  sf_internal_predicateOutput = after(CANNM_REPEAT_MEG_TIME,msec); */
          if ((uint32_T)((int32_T)AutoSarNm_DW.temporalCounter_i1 * 10) >=
              CANNM_REPEAT_MEG_TIME) {
            /* :  sf_internal_predicateOutput = AutoSarNmKeepSrc ~= 0; */
            if (BitwiseOR != 0) {
              AutoSarNm_DW.is_Set_NmSendMode = AutoSarNm_IN_NO_ACTIVE_CHILD;
              AutoSarNm_DW.is_Set_AppSendMode = AutoSarNm_IN_NO_ACTIVE_CHILD;
              AutoSarNm_DW.is_ALL = AutoSarNm_IN_NOS;

              /* :  AutoSarNM_sts = uint8(3); */
              AutoSarNM_sts_a = 3U;
            } else {
              AutoSarNm_DW.is_Set_NmSendMode = AutoSarNm_IN_NO_ACTIVE_CHILD;
              AutoSarNm_DW.is_Set_AppSendMode = AutoSarNm_IN_NO_ACTIVE_CHILD;
              AutoSarNm_DW.is_ALL = AutoSarNm_IN_RSS;

              /* :  AutoSarNM_sts = uint8(4); */
              AutoSarNM_sts_a = 4U;
              AutoSarNm_enter_internal_RSS();
            }
          } else {
            if ((AutoSarNm_DW.is_Set_AppSendMode == AutoSarNm_IN_Fast) &&
                (CANNM_cntAuroSarNMFrameSendCnt > 0U)) {
              /* :  sf_internal_predicateOutput = CANNM_cntAuroSarNMFrameSendCnt > 0; */
              AutoSarNm_DW.is_Set_AppSendMode = AutoSarNm_IN_Slow;

              /* :  NM_SetAppMsgEnable(uint8(1)); */
              NM_SetAppMsgEnable(1U);

              /* :  App_SendFlg = 1; */
              AutoSarNm_DW.App_SendFlg = 1U;
            } else {
              /* case IN_Slow: */
            }

            if (AutoSarNm_DW.is_Set_NmSendMode == AutoSarNm_IN_Fast) {
              /* :  sf_internal_predicateOutput = CANNM_cntAuroSarNMFrameSendCnt - RMSSendFrameCntDelay >= CANNM_IMMEDIATE_TRANSMISSION_COUNT; */
              qY = CANNM_cntAuroSarNMFrameSendCnt - /*MW:OvSatOk*/
                AutoSarNm_DW.RMSSendFrameCntDelay;
              if (qY > CANNM_cntAuroSarNMFrameSendCnt) {
                qY = 0U;
              }

              if (qY >= CANNM_IMMEDIATE_TRANSMISSION_COUNT) {
                AutoSarNm_DW.is_Set_NmSendMode = AutoSarNm_IN_Slow;

                /* :  NM_SetNmMsgEnable(uint8(1)); */
                NM_SetNmMsgEnable(1U);

                /* :  NM_SetNmMsgCycle(CANNM_MSG_CYCLE_TIME); */
                NM_SetNmMsgCycle(CANNM_MSG_CYCLE_TIME);
              }
            } else {
              /* case IN_Slow: */
            }
          }
          break;

         default:
          /* case IN_RSS: */
          AutoSarNM_sts_a = 4U;

          /* :  sf_internal_predicateOutput = AutoSarNmKeepSrc ~= 0; */
          if (BitwiseOR != 0) {
            /* :  NM_SetNmMsgEnable(uint8(1)); */
            NM_SetNmMsgEnable(1U);
            AutoSarNm_DW.is_SendLastFrame = AutoSarNm_IN_NO_ACTIVE_CHILD;
            AutoSarNm_DW.is_ALL = AutoSarNm_IN_NOS;

            /* :  AutoSarNM_sts = uint8(3); */
            AutoSarNM_sts_a = 3U;
          } else {
            if (AutoSarNm_DW.is_SendLastFrame == AutoSarNm_IN_end) {
              AutoSarNm_B.NmRdy2Slp = 0U;
            } else {
              /* case IN_wait: */
              AutoSarNm_B.NmRdy2Slp = 1U;

              /* :  sf_internal_predicateOutput = RMSSendFrameCntDelay ~= CANNM_cntAuroSarNMFrameSendCnt; */
              if (AutoSarNm_DW.RMSSendFrameCntDelay !=
                  CANNM_cntAuroSarNMFrameSendCnt) {
                AutoSarNm_DW.is_SendLastFrame = AutoSarNm_IN_end;

                /* :  NmRdy2Slp = 0; */
                AutoSarNm_B.NmRdy2Slp = 0U;

                /* :  NM_SetNmMsgEnable(uint8(0)); */
                NM_SetNmMsgEnable(0U);
              }
            }

            /* :  sf_internal_predicateOutput = after(CANNM_NM_TIMEOUT_TIME,msec); */
            if ((uint32_T)((int32_T)AutoSarNm_DW.temporalCounter_i1 * 10) >=
                CANNM_NM_TIMEOUT_TIME) {
              AutoSarNm_exit_internal_ALL();
              AutoSarNm_DW.is_c3_AutoSarNm = AutoSarNm_IN_PBSM;
              AutoSarNm_DW.temporalCounter_i1 = 0U;

              /* :  AutoSarNM_sts = uint8(1); */
              AutoSarNM_sts_a = 1U;

              /* :  NM_SetAppMsgEnable(uint8(0)); */
              NM_SetAppMsgEnable(0U);

              /* :  NM_ResetCanNmSendCnt(); */
              NM_ResetCanNmSendCnt();

              /* :  App_SendFlg = 0; */
              AutoSarNm_DW.App_SendFlg = 0U;

              /* :  NM_PbsmGetWkupMode(uint8(1)); */
              NM_PbsmGetWkupMode(1U);

              /* :  sf_internal_predicateOutput = CANNM_cntAuroSarNMFrameSendCnt_Step ~= 0; */
            } else if (FixPtRelationalOperator != 0) {
              AutoSarNm_DW.temporalCounter_i1 = 0U;
            }
          }
          break;
        }
      }
      break;

     case AutoSarNm_IN_BSM:
      AutoSarNM_sts_a = 0U;
      AutoSarNm_B.NmRdy2Slp = 0U;

      /* :  sf_internal_predicateOutput = AutoSarNM_stWKUPMode ~= hex2dec('0xFF'); */
      if (AutoSarNM_stWKUPMode != 255) {
        AutoSarNm_DW.is_BSM = AutoSarNm_IN_NO_ACTIVE_CHILD;
        AutoSarNm_DW.is_c3_AutoSarNm = AutoSarNm_IN_ALL;
        AutoSarNm_DW.is_ALL = AutoSarNm_IN_RMS;
        AutoSarNm_DW.temporalCounter_i1 = 0U;

        /* :  AutoSarNM_sts = uint8(2); */
        AutoSarNM_sts_a = 2U;

        /* 1、上电后首帧必须是网管管理帧 */
        /* 2、被动唤醒不进入快发模式 */
        AutoSarNm_enter_internal_RMS(&FixPtRelationalOperator_p);
      } else {
        /* :  NM_PbsmGetWkupMode(uint8(0)); */
        NM_PbsmGetWkupMode(0U);
        if ((AutoSarNm_DW.is_BSM == AutoSarNm_IN_wait) && ((uint32_T)((int32_T)
              AutoSarNm_DW.temporalCounter_i1 * 10) >= CANNM_BSM_WAITSLP_TIME))
        {
          /* :  sf_internal_predicateOutput = after(CANNM_BSM_WAITSLP_TIME,msec); */
          AutoSarNm_DW.is_BSM = AutoSarNm_IN_wait1;

          /* :  NM_EnterSleepMode(); */
          NM_EnterSleepMode();
        } else {
          /* case IN_wait1: */
        }
      }
      break;

     default:
      /* case IN_PBSM: */
      AutoSarNM_sts_a = 1U;

      /* :  sf_internal_predicateOutput = AutoSarNM_stWKUPMode ~= hex2dec('0xFF'); */
      if (AutoSarNM_stWKUPMode != 255) {
        AutoSarNm_DW.is_c3_AutoSarNm = AutoSarNm_IN_ALL;
        AutoSarNm_DW.is_ALL = AutoSarNm_IN_RMS;
        AutoSarNm_DW.temporalCounter_i1 = 0U;

        /* :  AutoSarNM_sts = uint8(2); */
        AutoSarNM_sts_a = 2U;

        /* 1、上电后首帧必须是网管管理帧 */
        /* 2、被动唤醒不进入快发模式 */
        AutoSarNm_enter_internal_RMS(&FixPtRelationalOperator_p);

        /* :  sf_internal_predicateOutput = after(CANNM_WAIT_BUS_SLEEP_TIME,msec); */
      } else if ((uint32_T)((int32_T)AutoSarNm_DW.temporalCounter_i1 * 10) >=
                 CANNM_WAIT_BUS_SLEEP_TIME) {
        AutoSarNm_DW.is_c3_AutoSarNm = AutoSarNm_IN_BSM;

        /* :  NM_SetNmMsgEnable(uint8(0)); */
        NM_SetNmMsgEnable(0U);

        /* :  NM_SetAppMsgEnable(uint8(0)); */
        NM_SetAppMsgEnable(0U);

        /* :  NM_ResetCanNmSendCnt(); */
        NM_ResetCanNmSendCnt();

        /* :  App_SendFlg = 0; */
        AutoSarNm_DW.App_SendFlg = 0U;

        /* :  AutoSarNM_sts = uint8(0); */
        AutoSarNM_sts_a = 0U;

        /* :  NmRdy2Slp = 0; */
        AutoSarNm_B.NmRdy2Slp = 0U;

        /* :  NM_PbsmGetWkupMode(uint8(1)); */
        NM_PbsmGetWkupMode(1U);
        AutoSarNm_DW.is_BSM = AutoSarNm_IN_wait1;

        /* :  NM_EnterSleepMode(); */
        NM_EnterSleepMode();
      } else {
        /* :  NM_PbsmGetWkupMode(uint8(0)); */
        NM_PbsmGetWkupMode(0U);
      }
      break;
    }
  }

  /* End of Chart: '<Root>/Chart' */

  /* SignalConversion generated from: '<Root>/Variant Source1' */
#if AUROSAR_NM_RDY2SLP_ENABLE == 1

  /* VariantMerge generated from: '<Root>/Variant Source1' */
  AutoSarNm_B.VariantMerge_For_Variant_Source = AutoSarNm_B.NmRdy2Slp;

#else

  /* VariantMerge generated from: '<Root>/Variant Source1' incorporates:
   *  Constant: '<Root>/Constant29'
   *  SignalConversion generated from: '<Root>/Variant Source1'
   */
  AutoSarNm_B.VariantMerge_For_Variant_Source = 0U;

#endif

  /* End of SignalConversion generated from: '<Root>/Variant Source1' */

  /* DataTypeConversion: '<Root>/Data Type Conversion6' incorporates:
   *  Constant: '<Root>/Constant8'
   */
  AutoSarNm_B.DataTypeConversion6 = (uint8_T)((uint32_T)CAN_ID_VEH_TX_NM);

  /* S-Function (scanfdpack): '<Root>/CAN FD Pack' incorporates:
   *  Constant: '<Root>/Constant10'
   *  Constant: '<Root>/Constant11'
   *  Constant: '<Root>/Constant12'
   *  Constant: '<Root>/Constant14'
   *  Constant: '<Root>/Constant15'
   *  Constant: '<Root>/Constant16'
   *  Constant: '<Root>/Constant17'
   *  Constant: '<Root>/Constant18'
   *  Constant: '<Root>/Constant19'
   *  Constant: '<Root>/Constant20'
   *  Constant: '<Root>/Constant9'
   */
  /* S-Function (scanfdpack): '<Root>/CAN FD Pack' */
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.ID = CAN_ID_VEH_TX_NM;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Length = 8U;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Extended = 1U;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Remote = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.BRS = 1;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.ProtocolMode = 1;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.DLC = 8U;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[0] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[1] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[2] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[3] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[4] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[5] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[6] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[7] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[8] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[9] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[10] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[11] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[12] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[13] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[14] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[15] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[16] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[17] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[18] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[19] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[20] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[21] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[22] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[23] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[24] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[25] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[26] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[27] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[28] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[29] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[30] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[31] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[32] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[33] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[34] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[35] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[36] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[37] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[38] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[39] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[40] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[41] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[42] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[43] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[44] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[45] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[46] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[47] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[48] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[49] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[50] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[51] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[52] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[53] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[54] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[55] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[56] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[57] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[58] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[59] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[60] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[61] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[62] = 0;
  AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[63] = 0;

  {
    /* --------------- START Packing signal 0 ------------------
     *  startBit                = 12
     *  length                  = 1
     *  desiredSignalByteLayout = BIGENDIAN
     *  dataType                = UNSIGNED
     *  factor                  = 1.0
     *  offset                  = 0.0
     *  minimum                 = 0.0
     *  maximum                 = 0.0
     * -----------------------------------------------------------------------*/
    {
      uint32_T packingValue = 0;

      {
        uint32_T result = (uint32_T) (AutoSarNm_B.Compare);

        /* no scaling required */
        packingValue = result;
      }

      {
        uint8_T packedValue;
        if (packingValue > (uint8_T)(1)) {
          packedValue = (uint8_T) 1;
        } else {
          packedValue = (uint8_T) (packingValue);
        }

        {
          {
            AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[1] =
              AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[1] | (uint8_T)((uint8_T)
              ((uint8_T)(packedValue & (uint8_T)0x1U) << 4));
          }
        }
      }
    }

    /* --------------- START Packing signal 1 ------------------
     *  startBit                = 9
     *  length                  = 2
     *  desiredSignalByteLayout = BIGENDIAN
     *  dataType                = UNSIGNED
     *  factor                  = 1.0
     *  offset                  = 0.0
     *  minimum                 = 0.0
     *  maximum                 = 0.0
     * -----------------------------------------------------------------------*/
    {
      uint32_T packingValue = 0;

      {
        uint32_T result = (uint32_T) (((uint8_T)0U));

        /* no scaling required */
        packingValue = result;
      }

      {
        uint8_T packedValue;
        if (packingValue > (uint8_T)(3)) {
          packedValue = (uint8_T) 3;
        } else {
          packedValue = (uint8_T) (packingValue);
        }

        {
          {
            AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[1] =
              AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[1] | (uint8_T)((uint8_T)
              ((uint8_T)(packedValue & (uint8_T)0x3U) << 1));
          }
        }
      }
    }

    /* --------------- START Packing signal 2 ------------------
     *  startBit                = 11
     *  length                  = 1
     *  desiredSignalByteLayout = BIGENDIAN
     *  dataType                = UNSIGNED
     *  factor                  = 1.0
     *  offset                  = 0.0
     *  minimum                 = 0.0
     *  maximum                 = 0.0
     * -----------------------------------------------------------------------*/
    {
      uint32_T packingValue = 0;

      {
        uint32_T result = (uint32_T)
          (AutoSarNm_B.VariantMerge_For_Variant_Source);

        /* no scaling required */
        packingValue = result;
      }

      {
        uint8_T packedValue;
        if (packingValue > (uint8_T)(1)) {
          packedValue = (uint8_T) 1;
        } else {
          packedValue = (uint8_T) (packingValue);
        }

        {
          {
            AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[1] =
              AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[1] | (uint8_T)((uint8_T)
              ((uint8_T)(packedValue & (uint8_T)0x1U) << 3));
          }
        }
      }
    }

    /* --------------- START Packing signal 3 ------------------
     *  startBit                = 14
     *  length                  = 1
     *  desiredSignalByteLayout = BIGENDIAN
     *  dataType                = UNSIGNED
     *  factor                  = 1.0
     *  offset                  = 0.0
     *  minimum                 = 0.0
     *  maximum                 = 0.0
     * -----------------------------------------------------------------------*/
    {
      uint32_T packingValue = 0;

      {
        uint32_T result = (uint32_T) (((uint8_T)0U));

        /* no scaling required */
        packingValue = result;
      }

      {
        uint8_T packedValue;
        if (packingValue > (uint8_T)(1)) {
          packedValue = (uint8_T) 1;
        } else {
          packedValue = (uint8_T) (packingValue);
        }

        {
          {
            AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[1] =
              AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[1] | (uint8_T)((uint8_T)
              ((uint8_T)(packedValue & (uint8_T)0x1U) << 6));
          }
        }
      }
    }

    /* --------------- START Packing signal 4 ------------------
     *  startBit                = 13
     *  length                  = 1
     *  desiredSignalByteLayout = BIGENDIAN
     *  dataType                = UNSIGNED
     *  factor                  = 1.0
     *  offset                  = 0.0
     *  minimum                 = 0.0
     *  maximum                 = 0.0
     * -----------------------------------------------------------------------*/
    {
      uint32_T packingValue = 0;

      {
        uint32_T result = (uint32_T) (((uint8_T)0U));

        /* no scaling required */
        packingValue = result;
      }

      {
        uint8_T packedValue;
        if (packingValue > (uint8_T)(1)) {
          packedValue = (uint8_T) 1;
        } else {
          packedValue = (uint8_T) (packingValue);
        }

        {
          {
            AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[1] =
              AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[1] | (uint8_T)((uint8_T)
              ((uint8_T)(packedValue & (uint8_T)0x1U) << 5));
          }
        }
      }
    }

    /* --------------- START Packing signal 5 ------------------
     *  startBit                = 15
     *  length                  = 1
     *  desiredSignalByteLayout = BIGENDIAN
     *  dataType                = UNSIGNED
     *  factor                  = 1.0
     *  offset                  = 0.0
     *  minimum                 = 0.0
     *  maximum                 = 0.0
     * -----------------------------------------------------------------------*/
    {
      uint32_T packingValue = 0;

      {
        uint32_T result = (uint32_T) (((uint8_T)0U));

        /* no scaling required */
        packingValue = result;
      }

      {
        uint8_T packedValue;
        if (packingValue > (uint8_T)(1)) {
          packedValue = (uint8_T) 1;
        } else {
          packedValue = (uint8_T) (packingValue);
        }

        {
          {
            AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[1] =
              AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[1] | (uint8_T)((uint8_T)
              ((uint8_T)(packedValue & (uint8_T)0x1U) << 7));
          }
        }
      }
    }

    /* --------------- START Packing signal 6 ------------------
     *  startBit                = 8
     *  length                  = 1
     *  desiredSignalByteLayout = BIGENDIAN
     *  dataType                = UNSIGNED
     *  factor                  = 1.0
     *  offset                  = 0.0
     *  minimum                 = 0.0
     *  maximum                 = 0.0
     * -----------------------------------------------------------------------*/
    {
      uint32_T packingValue = 0;

      {
        uint32_T result = (uint32_T) (((uint8_T)0U));

        /* no scaling required */
        packingValue = result;
      }

      {
        uint8_T packedValue;
        if (packingValue > (uint8_T)(1)) {
          packedValue = (uint8_T) 1;
        } else {
          packedValue = (uint8_T) (packingValue);
        }

        {
          {
            AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[1] =
              AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[1] | (uint8_T)((uint8_T)
              (packedValue & (uint8_T)0x1U));
          }
        }
      }
    }

    /* --------------- START Packing signal 7 ------------------
     *  startBit                = 0
     *  length                  = 8
     *  desiredSignalByteLayout = BIGENDIAN
     *  dataType                = UNSIGNED
     *  factor                  = 1.0
     *  offset                  = 0.0
     *  minimum                 = 0.0
     *  maximum                 = 0.0
     * -----------------------------------------------------------------------*/
    {
      uint32_T packingValue = 0;

      {
        uint32_T result = (uint32_T) (AutoSarNm_B.DataTypeConversion6);

        /* no scaling required */
        packingValue = result;
      }

      {
        uint8_T packedValue;
        packedValue = (uint8_T) (packingValue);

        {
          {
            AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[0] =
              AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[0] | (uint8_T)
              (packedValue);
          }
        }
      }
    }

    /* --------------- START Packing signal 8 ------------------
     *  startBit                = 16
     *  length                  = 8
     *  desiredSignalByteLayout = BIGENDIAN
     *  dataType                = UNSIGNED
     *  factor                  = 1.0
     *  offset                  = 0.0
     *  minimum                 = 0.0
     *  maximum                 = 0.0
     * -----------------------------------------------------------------------*/
    {
      uint32_T packingValue = 0;

      {
        uint32_T result = (uint32_T) (((uint8_T)0U));

        /* no scaling required */
        packingValue = result;
      }

      {
        uint8_T packedValue;
        packedValue = (uint8_T) (packingValue);

        {
          {
            AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[2] =
              AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[2] | (uint8_T)
              (packedValue);
          }
        }
      }
    }

    /* --------------- START Packing signal 9 ------------------
     *  startBit                = 24
     *  length                  = 8
     *  desiredSignalByteLayout = BIGENDIAN
     *  dataType                = UNSIGNED
     *  factor                  = 1.0
     *  offset                  = 0.0
     *  minimum                 = 0.0
     *  maximum                 = 0.0
     * -----------------------------------------------------------------------*/
    {
      uint32_T packingValue = 0;

      {
        uint32_T result = (uint32_T) (((uint8_T)0U));

        /* no scaling required */
        packingValue = result;
      }

      {
        uint8_T packedValue;
        packedValue = (uint8_T) (packingValue);

        {
          {
            AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[3] =
              AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[3] | (uint8_T)
              (packedValue);
          }
        }
      }
    }

    /* --------------- START Packing signal 10 ------------------
     *  startBit                = 32
     *  length                  = 8
     *  desiredSignalByteLayout = BIGENDIAN
     *  dataType                = UNSIGNED
     *  factor                  = 1.0
     *  offset                  = 0.0
     *  minimum                 = 0.0
     *  maximum                 = 0.0
     * -----------------------------------------------------------------------*/
    {
      uint32_T packingValue = 0;

      {
        uint32_T result = (uint32_T) (((uint8_T)0U));

        /* no scaling required */
        packingValue = result;
      }

      {
        uint8_T packedValue;
        packedValue = (uint8_T) (packingValue);

        {
          {
            AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[4] =
              AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[4] | (uint8_T)
              (packedValue);
          }
        }
      }
    }

    /* --------------- START Packing signal 11 ------------------
     *  startBit                = 40
     *  length                  = 8
     *  desiredSignalByteLayout = BIGENDIAN
     *  dataType                = UNSIGNED
     *  factor                  = 1.0
     *  offset                  = 0.0
     *  minimum                 = 0.0
     *  maximum                 = 0.0
     * -----------------------------------------------------------------------*/
    {
      uint32_T packingValue = 0;

      {
        uint32_T result = (uint32_T) (((uint8_T)0U));

        /* no scaling required */
        packingValue = result;
      }

      {
        uint8_T packedValue;
        packedValue = (uint8_T) (packingValue);

        {
          {
            AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[5] =
              AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[5] | (uint8_T)
              (packedValue);
          }
        }
      }
    }

    /* --------------- START Packing signal 12 ------------------
     *  startBit                = 48
     *  length                  = 8
     *  desiredSignalByteLayout = BIGENDIAN
     *  dataType                = UNSIGNED
     *  factor                  = 1.0
     *  offset                  = 0.0
     *  minimum                 = 0.0
     *  maximum                 = 0.0
     * -----------------------------------------------------------------------*/
    {
      uint32_T packingValue = 0;

      {
        uint32_T result = (uint32_T) (((uint8_T)0U));

        /* no scaling required */
        packingValue = result;
      }

      {
        uint8_T packedValue;
        packedValue = (uint8_T) (packingValue);

        {
          {
            AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[6] =
              AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[6] | (uint8_T)
              (packedValue);
          }
        }
      }
    }

    /* --------------- START Packing signal 13 ------------------
     *  startBit                = 56
     *  length                  = 8
     *  desiredSignalByteLayout = BIGENDIAN
     *  dataType                = UNSIGNED
     *  factor                  = 1.0
     *  offset                  = 0.0
     *  minimum                 = 0.0
     *  maximum                 = 0.0
     * -----------------------------------------------------------------------*/
    {
      uint32_T packingValue = 0;

      {
        uint32_T result = (uint32_T) (((uint8_T)0U));

        /* no scaling required */
        packingValue = result;
      }

      {
        uint8_T packedValue;
        packedValue = (uint8_T) (packingValue);

        {
          {
            AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[7] =
              AutoSarNm_B.CAN_msgAutoSarNmFrame_i.Data[7] | (uint8_T)
              (packedValue);
          }
        }
      }
    }
  }

  /* DataStoreWrite: '<Root>/Data Store Write' incorporates:
   *  S-Function (scanfdpack): '<Root>/CAN FD Pack'
   */
  CAN_msgAutoSarNmFrame = AutoSarNm_B.CAN_msgAutoSarNmFrame_i;

  /* DataTypeConversion: '<Root>/Data Type Conversion1' */
  AutoSarNM_sts = (AutoSarNm_Sts_Enum)AutoSarNM_sts_a;

  /* Update for UnitDelay: '<S7>/Delay Input1' incorporates:
   *  Inport: '<Root>/CANNM_cntAuroSarNMFrameSendCnt'
   *
   * Block description for '<S7>/Delay Input1':
   *
   *  Store in Global RAM
   */
  AutoSarNm_DW.DelayInput1_DSTATE = CANNM_cntAuroSarNMFrameSendCnt;

  /* Update for UnitDelay: '<S5>/Delay Input1' incorporates:
   *  Inport: '<Root>/AutoSarNM_cntUDSMsgRxCnt'
   *
   * Block description for '<S5>/Delay Input1':
   *
   *  Store in Global RAM
   */
  AutoSarNm_DW.DelayInput1_DSTATE_h = AutoSarNM_cntUDSMsgRxCnt;

  /* Update for UnitDelay: '<Root>/Unit Delay' */
  AutoSarNm_DW.UnitDelay_DSTATE = rtb_Switch;

  /* Update for UnitDelay: '<S6>/Delay Input1' incorporates:
   *  Inport: '<Root>/CANNM_stRMSReq'
   *
   * Block description for '<S6>/Delay Input1':
   *
   *  Store in Global RAM
   */
  AutoSarNm_DW.DelayInput1_DSTATE_d = CANNM_stRMSReq;
}

/* Model initialize function */
void AutoSarNm_initialize(void)
{
  /* Outputs for Atomic SubSystem: '<Root>/Initialize Function' */
  /* Outputs for Atomic SubSystem: '<S8>/Atomic Subsystem' */
  /* S-Function (scanfdpack): '<S9>/CAN FD Pack' incorporates:
   *  Constant: '<S9>/Constant1'
   */
  /* S-Function (scanfdpack): '<S9>/CAN FD Pack' */
  AutoSarNm_B.CANFDPack.ID = CAN_ID_VEH_TX_NM;
  AutoSarNm_B.CANFDPack.Length = 8U;
  AutoSarNm_B.CANFDPack.Extended = 1U;
  AutoSarNm_B.CANFDPack.Remote = 0;
  AutoSarNm_B.CANFDPack.BRS = 1;
  AutoSarNm_B.CANFDPack.ProtocolMode = 1;
  AutoSarNm_B.CANFDPack.DLC = 8U;
  AutoSarNm_B.CANFDPack.Data[0] = 0;
  AutoSarNm_B.CANFDPack.Data[1] = 0;
  AutoSarNm_B.CANFDPack.Data[2] = 0;
  AutoSarNm_B.CANFDPack.Data[3] = 0;
  AutoSarNm_B.CANFDPack.Data[4] = 0;
  AutoSarNm_B.CANFDPack.Data[5] = 0;
  AutoSarNm_B.CANFDPack.Data[6] = 0;
  AutoSarNm_B.CANFDPack.Data[7] = 0;
  AutoSarNm_B.CANFDPack.Data[8] = 0;
  AutoSarNm_B.CANFDPack.Data[9] = 0;
  AutoSarNm_B.CANFDPack.Data[10] = 0;
  AutoSarNm_B.CANFDPack.Data[11] = 0;
  AutoSarNm_B.CANFDPack.Data[12] = 0;
  AutoSarNm_B.CANFDPack.Data[13] = 0;
  AutoSarNm_B.CANFDPack.Data[14] = 0;
  AutoSarNm_B.CANFDPack.Data[15] = 0;
  AutoSarNm_B.CANFDPack.Data[16] = 0;
  AutoSarNm_B.CANFDPack.Data[17] = 0;
  AutoSarNm_B.CANFDPack.Data[18] = 0;
  AutoSarNm_B.CANFDPack.Data[19] = 0;
  AutoSarNm_B.CANFDPack.Data[20] = 0;
  AutoSarNm_B.CANFDPack.Data[21] = 0;
  AutoSarNm_B.CANFDPack.Data[22] = 0;
  AutoSarNm_B.CANFDPack.Data[23] = 0;
  AutoSarNm_B.CANFDPack.Data[24] = 0;
  AutoSarNm_B.CANFDPack.Data[25] = 0;
  AutoSarNm_B.CANFDPack.Data[26] = 0;
  AutoSarNm_B.CANFDPack.Data[27] = 0;
  AutoSarNm_B.CANFDPack.Data[28] = 0;
  AutoSarNm_B.CANFDPack.Data[29] = 0;
  AutoSarNm_B.CANFDPack.Data[30] = 0;
  AutoSarNm_B.CANFDPack.Data[31] = 0;
  AutoSarNm_B.CANFDPack.Data[32] = 0;
  AutoSarNm_B.CANFDPack.Data[33] = 0;
  AutoSarNm_B.CANFDPack.Data[34] = 0;
  AutoSarNm_B.CANFDPack.Data[35] = 0;
  AutoSarNm_B.CANFDPack.Data[36] = 0;
  AutoSarNm_B.CANFDPack.Data[37] = 0;
  AutoSarNm_B.CANFDPack.Data[38] = 0;
  AutoSarNm_B.CANFDPack.Data[39] = 0;
  AutoSarNm_B.CANFDPack.Data[40] = 0;
  AutoSarNm_B.CANFDPack.Data[41] = 0;
  AutoSarNm_B.CANFDPack.Data[42] = 0;
  AutoSarNm_B.CANFDPack.Data[43] = 0;
  AutoSarNm_B.CANFDPack.Data[44] = 0;
  AutoSarNm_B.CANFDPack.Data[45] = 0;
  AutoSarNm_B.CANFDPack.Data[46] = 0;
  AutoSarNm_B.CANFDPack.Data[47] = 0;
  AutoSarNm_B.CANFDPack.Data[48] = 0;
  AutoSarNm_B.CANFDPack.Data[49] = 0;
  AutoSarNm_B.CANFDPack.Data[50] = 0;
  AutoSarNm_B.CANFDPack.Data[51] = 0;
  AutoSarNm_B.CANFDPack.Data[52] = 0;
  AutoSarNm_B.CANFDPack.Data[53] = 0;
  AutoSarNm_B.CANFDPack.Data[54] = 0;
  AutoSarNm_B.CANFDPack.Data[55] = 0;
  AutoSarNm_B.CANFDPack.Data[56] = 0;
  AutoSarNm_B.CANFDPack.Data[57] = 0;
  AutoSarNm_B.CANFDPack.Data[58] = 0;
  AutoSarNm_B.CANFDPack.Data[59] = 0;
  AutoSarNm_B.CANFDPack.Data[60] = 0;
  AutoSarNm_B.CANFDPack.Data[61] = 0;
  AutoSarNm_B.CANFDPack.Data[62] = 0;
  AutoSarNm_B.CANFDPack.Data[63] = 0;

  {
    /* --------------- START Packing signal 0 ------------------
     *  startBit                = 0
     *  length                  = 32
     *  desiredSignalByteLayout = LITTLEENDIAN
     *  dataType                = SIGNED
     *  factor                  = 1.0
     *  offset                  = 0.0
     *  minimum                 = 0.0
     *  maximum                 = 0.0
     * -----------------------------------------------------------------------*/
    {
      uint32_T packingValue = 0;

      {
        uint32_T result = (uint32_T) (((uint8_T)0U));

        /* no scaling required */
        packingValue = result;
      }

      {
        int32_T packedValue;
        int32_T scaledValue;
        scaledValue = (int32_T) packingValue;
        packedValue = (int32_T) (scaledValue);

        {
          uint32_T* tempValuePtr = (uint32_T*)&packedValue;
          uint32_T tempValue = *tempValuePtr;

          {
            AutoSarNm_B.CANFDPack.Data[0] = AutoSarNm_B.CANFDPack.Data[0] |
              (uint8_T)((uint32_T)(tempValue & (uint32_T)0xFFU));
            AutoSarNm_B.CANFDPack.Data[1] = AutoSarNm_B.CANFDPack.Data[1] |
              (uint8_T)((uint32_T)((uint32_T)(tempValue & (uint32_T)0xFF00U) >>
              8));
            AutoSarNm_B.CANFDPack.Data[2] = AutoSarNm_B.CANFDPack.Data[2] |
              (uint8_T)((uint32_T)((uint32_T)(tempValue & (uint32_T)0xFF0000U) >>
              16));
            AutoSarNm_B.CANFDPack.Data[3] = AutoSarNm_B.CANFDPack.Data[3] |
              (uint8_T)((uint32_T)((uint32_T)(tempValue & (uint32_T)0xFF000000U)
              >> 24));
          }
        }
      }
    }

    /* --------------- START Packing signal 1 ------------------
     *  startBit                = 32
     *  length                  = 32
     *  desiredSignalByteLayout = LITTLEENDIAN
     *  dataType                = SIGNED
     *  factor                  = 1.0
     *  offset                  = 0.0
     *  minimum                 = 0.0
     *  maximum                 = 0.0
     * -----------------------------------------------------------------------*/
    {
      uint32_T packingValue = 0;

      {
        uint32_T result = (uint32_T) (((uint8_T)0U));

        /* no scaling required */
        packingValue = result;
      }

      {
        int32_T packedValue;
        int32_T scaledValue;
        scaledValue = (int32_T) packingValue;
        packedValue = (int32_T) (scaledValue);

        {
          uint32_T* tempValuePtr = (uint32_T*)&packedValue;
          uint32_T tempValue = *tempValuePtr;

          {
            AutoSarNm_B.CANFDPack.Data[4] = AutoSarNm_B.CANFDPack.Data[4] |
              (uint8_T)((uint32_T)(tempValue & (uint32_T)0xFFU));
            AutoSarNm_B.CANFDPack.Data[5] = AutoSarNm_B.CANFDPack.Data[5] |
              (uint8_T)((uint32_T)((uint32_T)(tempValue & (uint32_T)0xFF00U) >>
              8));
            AutoSarNm_B.CANFDPack.Data[6] = AutoSarNm_B.CANFDPack.Data[6] |
              (uint8_T)((uint32_T)((uint32_T)(tempValue & (uint32_T)0xFF0000U) >>
              16));
            AutoSarNm_B.CANFDPack.Data[7] = AutoSarNm_B.CANFDPack.Data[7] |
              (uint8_T)((uint32_T)((uint32_T)(tempValue & (uint32_T)0xFF000000U)
              >> 24));
          }
        }
      }
    }
  }

  /* DataStoreWrite: '<S9>/Data Store Write' incorporates:
   *  S-Function (scanfdpack): '<S9>/CAN FD Pack'
   */
  CAN_msgAutoSarNmFrame = AutoSarNm_B.CANFDPack;

  /* End of Outputs for SubSystem: '<S8>/Atomic Subsystem' */

  /* Outputs for Atomic SubSystem: '<S8>/Atomic Subsystem1' */
  /* CCaller: '<S10>/C Caller' incorporates:
   *  Constant: '<S10>/Constant'
   */
  NM_SetAppMsgEnable(0U);

  /* CCaller: '<S10>/C Caller1' incorporates:
   *  Constant: '<S10>/Constant'
   */
  NM_SetNmMsgEnable(0U);

  /* End of Outputs for SubSystem: '<S8>/Atomic Subsystem1' */

  /* Outputs for Atomic SubSystem: '<S8>/Atomic Subsystem2' */
  /* CCaller: '<S11>/C Caller' */
  NM_BsmGetWkupMode();

  /* End of Outputs for SubSystem: '<S8>/Atomic Subsystem2' */
  /* End of Outputs for SubSystem: '<Root>/Initialize Function' */
}

/*
 * File trailer for generated code.
 *
 * [EOF]
 */
