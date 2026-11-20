/*
 * XCP.h
 *
 *  Created on: 2026年11月20日
 *      Author: CENSORED
 */

#ifndef XCP_H_
#define XCP_H_

#define XCP_ENABLE 1

#if XCP_ENABLE

#include <stdint.h>
#include "can_fd_message.h"

#define XCP_RX_ID 0x187A55AA
#define XCP_TX_ID 0X187BAA55

typedef enum{
	XCP_CONNECT = 0xFF,
	XCP_SET_MTA = 0xF6,
	XCP_DOWNLOARD = 0xF0,
	XCP_SHORTUPLOAD = 0xF4,
	XCP_DISCONNECT = 0xFE
}XCP_Command;

/*==================================================================================================
* XCP on CAN - 参数标定
*
* FlexCAN 配置为经典 CAN（payload 8 字节），一个 CTO/DTO 占一帧：
*
*   主机 -> 从机 (CTO)                 从机 -> 主机 (DTO)
*   CONNECT       FF 00                FF 00 00 08 08 00 01 01
*   DISCONNECT    FE                   FF 00
*   SET_MTA       F6 00 00 ext         FF                     (地址在 4..7, Intel)
*                 addr[4]
*   DOWNLOAD      F0 n data[n]         FF                     (MTA 自动后移)
*   SHORT_UPLOAD  F4 n 00 ext          FF data[n]             (地址在 4..7, Intel)
*                 addr[4]
*
* 否定应答固定为 “FE <错误码>”；一帧中没用到的字节填 0。
* 只支持这 5 条指令，且只有 .xcp_cal 段（位于 DTCM，见 XCP_Para.c）可以被访问。
*================================================================================================*/
#define XCP_MAX_CTO             (8U)                    /* 命令最大长度: 一帧 CAN          */
#define XCP_MAX_DTO             (8U)                    /* 应答最大长度: 一帧 CAN          */
#define XCP_MAX_DOWNLOAD_SIZE   (XCP_MAX_CTO - 2U)      /* DOWNLOAD 里数据字节数 (6)       */
#define XCP_MAX_UPLOAD_SIZE     (XCP_MAX_DTO - 1U)      /* SHORT_UPLOAD 应答数据字节数 (7) */

/* 应答包标识 */
#define XCP_PID_RES             0xFFU                   /* 肯定应答                        */
#define XCP_PID_ERR             0xFEU                   /* 否定应答/错误包                 */

/* 错误码 (ASAM MCD-1 XCP) */
#define XCP_ERR_CMD_UNKNOWN     0x20U                   /* 不支持的指令                    */
#define XCP_ERR_CMD_SYNTAX      0x21U                   /* 指令长度/参数错误               */
#define XCP_ERR_OUT_OF_RANGE    0x22U                   /* 地址超出可标定范围              */
#define XCP_ERR_ACCESS_LOCKED   0x25U                   /* 未连接 / 资源未解锁             */

/* CONNECT 应答里上报的协议层/传输层版本 */
#define XCP_PROTOCOL_LAYER_VERSION      (1U)
#define XCP_TRANSPORT_LAYER_VERSION     (1U)

/*
 * 所有可被 XCP 标定的对象都必须带这个属性：
 *   - 放进 .xcp_cal 段，链接脚本把它定位在 DTCM 内
 *     (S32K344_flash.ld / S32K344_ram.ld)；
 *   - used 防止链接器把没被 C 代码引用的标定量删掉。
 */
#define XCP_CAL_DATA    __attribute__((section(".xcp_cal"), used))

extern CAN_FD_MESSAGE XCP_REQ;

/*
 * 接收回调。BSW_CanRxCallout() 收到 XCP_RX_ID 的帧后调用它，
 * 参数就是本条请求（XCP_REQ），应答由 XCP.c 直接写 Can 驱动发送。
 */
void XCP_CallBack(CAN_FD_MESSAGE* data);

#endif/* XCP_ENABLE */

#endif /* XCP_H_ */
