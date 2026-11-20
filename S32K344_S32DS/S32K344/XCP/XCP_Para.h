/*
 * XCP_Para.h
 *
 *  Created on: 2026年11月20日
 *      Author: CENSORED
 *
 * 可标定参数（XCP calibration data）。
 *
 * 这里声明的对象全部在 XCP_Para.c 中定义，并且都带 XCP_CAL_DATA 属性，
 * 所以它们都在 DTCM 的 .xcp_cal 段内。XCP 只允许访问这一段：
 * SET_MTA / DOWNLOAD / SHORT_UPLOAD 的地址不在其中时一律拒绝。
 *
 * 增加一个可标定量的步骤：
 *   1) 在 XCP_Para.c 中定义：  XCP_CAL_DATA uint16 XCP_Cal_xxx = 默认值;
 *   2) 在本文件中加声明：       extern uint16 XCP_Cal_xxx;
 *   3) 在 .map 文件里查该变量在 .xcp_cal 段中的地址，填到 A2L 中；
 *      XCP 就是按这个运行地址直接读写标定量的。
 */

#ifndef XCP_PARA_H_
#define XCP_PARA_H_

#include <stdint.h>
#include "XCP.h"

#if XCP_ENABLE

/* 示例标定量：按上面 3 步换成/追加自己的参数即可。 */
extern uint8_t  XCP_Cal_u8Demo;
extern uint16_t XCP_Cal_u16Demo;
extern uint32_t XCP_Cal_u32Demo;
extern float    XCP_Cal_f32Demo;
extern uint8_t  XCP_Cal_au8Demo[8];

#endif /* XCP_ENABLE */

#endif /* XCP_PARA_H_ */
