/*
 * XCP_Para.c
 *
 *  Created on: 2026年11月20日
 *      Author: CENSORED
 *
 * 可标定参数的默认值。
 *
 * 这些对象带 XCP_CAL_DATA 属性 -> .xcp_cal 段 -> DTCM。
 * .xcp_cal 被放进链接脚本的 .data_tcm_data 输出段，启动代码会把它从 Flash
 * 拷贝到 DTCM，所以这里写的初值就是上电后的标定初值。
 * XCP 标定只改 DTCM 中的副本：掉电/复位后恢复成这里的默认值。
 */

#include "XCP_Para.h"
#include "XCP.h"

#if XCP_ENABLE

XCP_CAL_DATA uint8_t  XCP_Cal_u8Demo      = 0x5AU;
XCP_CAL_DATA uint16_t XCP_Cal_u16Demo     = 0x1234U;
XCP_CAL_DATA uint32_t XCP_Cal_u32Demo     = 0x12345678UL;
XCP_CAL_DATA float    XCP_Cal_f32Demo     = 1.0F;
XCP_CAL_DATA uint8_t  XCP_Cal_au8Demo[8]  = {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U};

#endif /* XCP_ENABLE */
