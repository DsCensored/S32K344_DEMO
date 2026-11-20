/*
 * XCP.c
 *
 *  Created on: 2026年11月20日
 *      Author: CENSORED
 *
 * XCP 从机（XCP on CAN），只做参数标定。
 *
 * 支持的指令（见 XCP.h）：CONNECT / DISCONNECT / SET_MTA / DOWNLOAD / SHORT_UPLOAD。
 *
 * 地址保护：
 *   只有链接脚本给出的 .xcp_cal 段（位于 DTCM）可以被访问。
 *   SET_MTA 的地址不在该范围时会回复 ERR_OUT_OF_RANGE 并让 MTA 失效，
 *   这样紧随其后的 DOWNLOAD 也会被拒绝，即“拒绝这次标定”。
 *   DOWNLOAD 还会检查 [MTA, MTA+n) 是否整个落在该范围内，防止越界写穿。
 *
 * 传输：
 *   请求由 BSW_CanRxCallout() 在 2ms 任务里送进来，应答就在这里直接调用
 *   Can_43_FLEXCAN_Write() 发出（和 UDS 应答同一个 HTH），不占用周期发送表。
 */

#include "XCP.h"

#if XCP_ENABLE

#include <string.h>
#include "Can_43_FLEXCAN.h"
#include "XCP_Para.h"

/* Can_IdType 里的“扩展帧”标志位 */
#define XCP_CAN_EXTENDED        (0x80000000UL)
/* 应答使用的硬件发送对象：与诊断/周期报文相同的 HTH (CanHardwareObject_1) */
#define XCP_TX_HTH              (1U)
/* XCPonCAN：一个 CTO/DTO 占一整帧 8 字节，没用到的字节填 0 */
#define XCP_RESPONSE_LENGTH     (8U)

/*
 * 标定窗口，由链接脚本提供（.xcp_cal 段，位于 DTCM 内）。
 * 除这一段以外，XCP 不访问任何其它内存。
 */
extern uint8_t __xcp_cal_start__[];
extern uint8_t __xcp_cal_end__[];

CAN_FD_MESSAGE XCP_REQ;

/* 连接状态与 MTA。只有 XCP_CallBack() 会修改它们。 */
static uint8_t  XCP_Connected = 0U;
static uint8_t  XCP_MtaValid  = 0U;
static uint32_t XCP_Mta       = 0U;

/*--------------------------------------------------------------------------------------------------
 * 内部函数
 *------------------------------------------------------------------------------------------------*/

/* Intel(小端) 方式的 32 位地址字段 */
static uint32_t XCP_ReadDword(const uint8_t* data)
{
	return ((uint32_t)data[0]) |
	       ((uint32_t)data[1] << 8) |
	       ((uint32_t)data[2] << 16) |
	       ((uint32_t)data[3] << 24);
}

/* [address, address + length) 完整落在 DTCM 标定窗口内时返回 1，否则返回 0 */
static uint8_t XCP_IsCalibrationAddress(uint32_t address, uint32_t length)
{
	uint32_t start = (uint32_t)__xcp_cal_start__;
	uint32_t end   = (uint32_t)__xcp_cal_end__;

	if ((address < start) || (address >= end))
	{
		return 0U;
	}

	if (length > (end - address))
	{
		return 0U;
	}

	return 1U;
}

/* 发送一帧应答。XCPonCAN 的 CTO/DTO 总是一整帧，短包尾随补 0。 */
static void XCP_SendFrame(const uint8_t* response, uint8_t length)
{
	uint8_t     frame[XCP_RESPONSE_LENGTH];
	Can_PduType pdu;
	uint8_t     index;

	if (length > XCP_RESPONSE_LENGTH)
	{
		length = XCP_RESPONSE_LENGTH;
	}

	for (index = 0U; index < XCP_RESPONSE_LENGTH; index++)
	{
		frame[index] = (index < length) ? response[index] : 0U;
	}

	pdu.id          = (Can_IdType)(((uint32_t)XCP_TX_ID) | XCP_CAN_EXTENDED);
	pdu.length      = (uint8)XCP_RESPONSE_LENGTH;
	pdu.sdu         = frame;
	pdu.swPduHandle = 0U;

	/* 如果没有空闲发送 buffer（周期报文正在发送），这一帧就丢掉：
	 * 主机在 T1 超时后会重发请求。 */
	(void)Can_43_FLEXCAN_Write((Can_HwHandleType)XCP_TX_HTH, &pdu);
}

/* 肯定应答：只有 PID */
static void XCP_SendPositive(void)
{
	uint8_t response[1];

	response[0] = XCP_PID_RES;

	XCP_SendFrame(response, 1U);
}

/* 否定应答：PID + 错误码 */
static void XCP_SendError(uint8_t errorCode)
{
	uint8_t response[2];

	response[0] = XCP_PID_ERR;
	response[1] = errorCode;

	XCP_SendFrame(response, 2U);
}

/*--------------------------------------------------------------------------------------------------
 * 指令处理
 *------------------------------------------------------------------------------------------------*/

/* CONNECT 0xFF：建立连接（每次连接都从“无有效 MTA”开始） */
static void XCP_CmdConnect(void)
{
	uint8_t response[XCP_RESPONSE_LENGTH];

	XCP_Connected = 1U;
	XCP_MtaValid  = 0U;
	XCP_Mta       = 0U;

	response[0] = XCP_PID_RES;
	response[1] = 0x00U;                                        /* RES                          */
	response[2] = 0x00U;                                        /* Intel, 字节粒度, 无 block    */
	response[3] = (uint8_t)XCP_MAX_CTO;
	response[4] = (uint8_t)(XCP_MAX_DTO & 0xFFU);
	response[5] = (uint8_t)((XCP_MAX_DTO >> 8) & 0xFFU);
	response[6] = (uint8_t)XCP_PROTOCOL_LAYER_VERSION;
	response[7] = (uint8_t)XCP_TRANSPORT_LAYER_VERSION;

	XCP_SendFrame(response, XCP_RESPONSE_LENGTH);
}

/* DISCONNECT 0xFE：断开连接 */
static void XCP_CmdDisconnect(void)
{
	uint8_t response[2];

	XCP_Connected = 0U;
	XCP_MtaValid  = 0U;
	XCP_Mta       = 0U;

	response[0] = XCP_PID_RES;
	response[1] = 0x00U;

	XCP_SendFrame(response, 2U);
}

/* SET_MTA 0xF6：设置下次 DOWNLOAD 的目标地址，地址必须在 DTCM 标定窗口内 */
static void XCP_CmdSetMta(const CAN_FD_MESSAGE* request)
{
	uint32_t address;

	if ((uint32_t)request->Length < XCP_MAX_CTO)
	{
		XCP_SendError(XCP_ERR_CMD_SYNTAX);
		return;
	}

	/* 请求: F6 00 00 ext addr[4] (Intel)
	 * 不支持地址扩展（paging），标定量直接用 32 位地址寻址。 */
	address = XCP_ReadDword(&request->Data[4]);

	if (0U == XCP_IsCalibrationAddress(address, 1U))
	{
		/* 不在 DTCM 标定窗口内：拒绝这次 SET_MTA，并让 MTA 失效，
		 * 保证随后的 DOWNLOAD 也写不进去。 */
		XCP_Mta      = 0U;
		XCP_MtaValid = 0U;
		XCP_SendError(XCP_ERR_OUT_OF_RANGE);
		return;
	}

	XCP_Mta      = address;
	XCP_MtaValid = 1U;
	XCP_SendPositive();
}

/* DOWNLOAD 0xF0：把数据写到 MTA，成功后 MTA 自动后移 */
static void XCP_CmdDownload(const CAN_FD_MESSAGE* request)
{
	uint8_t  count;
	uint32_t length;

	if ((uint32_t)request->Length < 2U)
	{
		XCP_SendError(XCP_ERR_CMD_SYNTAX);
		return;
	}

	count  = request->Data[1];
	length = (uint32_t)count;

	if ((0U == count) ||
	    (length > XCP_MAX_DOWNLOAD_SIZE) ||
	    ((length + 2U) > (uint32_t)request->Length))
	{
		XCP_SendError(XCP_ERR_CMD_SYNTAX);
		return;
	}

	/* SET_MTA 被拒绝后 MTA 无效；另外整块数据都必须落在标定窗口内。 */
	if ((0U == XCP_MtaValid) ||
	    (0U == XCP_IsCalibrationAddress(XCP_Mta, length)))
	{
		XCP_MtaValid = 0U;
		XCP_SendError(XCP_ERR_OUT_OF_RANGE);
		return;
	}

	memcpy((void*)(uintptr_t)XCP_Mta, &request->Data[2], (size_t)count);

	/* XCP：DOWNLOAD 之后 MTA += 写入的元素个数 */
	XCP_Mta += length;

	XCP_SendPositive();
}

/* SHORT_UPLOAD 0xF4：按请求里给出的地址直接读回标定量（不改变 MTA） */
static void XCP_CmdShortUpload(const CAN_FD_MESSAGE* request)
{
	uint8_t  count;
	uint32_t address;
	uint8_t  response[XCP_RESPONSE_LENGTH];

	if ((uint32_t)request->Length < XCP_MAX_CTO)
	{
		XCP_SendError(XCP_ERR_CMD_SYNTAX);
		return;
	}

	/* 请求: F4 n 00 ext addr[4] (Intel) */
	count = request->Data[1];

	if ((0U == count) || ((uint32_t)count > XCP_MAX_UPLOAD_SIZE))
	{
		XCP_SendError(XCP_ERR_CMD_SYNTAX);
		return;
	}

	address = XCP_ReadDword(&request->Data[4]);

	/* 读也只允许在 DTCM 标定窗口内 */
	if (0U == XCP_IsCalibrationAddress(address, (uint32_t)count))
	{
		XCP_SendError(XCP_ERR_OUT_OF_RANGE);
		return;
	}

	response[0] = XCP_PID_RES;
	memcpy(&response[1], (const void*)(uintptr_t)address, (size_t)count);

	XCP_SendFrame(response, (uint8_t)(count + 1U));
}

/*--------------------------------------------------------------------------------------------------
 * 接收回调
 *------------------------------------------------------------------------------------------------*/
void XCP_CallBack(CAN_FD_MESSAGE* data)
{
	if (NULL_PTR == data)
	{
		return;
	}

	/* RX 表里已经按 XCP_RX_ID 匹配过，这里再确认一次 ID 与帧类型，
	 * 避免 RX 表配错时误应答别的报文。 */
	if ((0U == data->Extended) || (data->ID != (uint32_T)XCP_RX_ID))
	{
		return;
	}

	if (0U == data->Length)
	{
		return;
	}

	switch (data->Data[0])
	{
	case (uint8_t)XCP_CONNECT:
		XCP_CmdConnect();
		break;

	case (uint8_t)XCP_DISCONNECT:
		if (0U == XCP_Connected)
		{
			XCP_SendError(XCP_ERR_ACCESS_LOCKED);
			break;
		}
		XCP_CmdDisconnect();
		break;

	case (uint8_t)XCP_SET_MTA:
		if (0U == XCP_Connected)
		{
			XCP_SendError(XCP_ERR_ACCESS_LOCKED);
			break;
		}
		XCP_CmdSetMta(data);
		break;

	case (uint8_t)XCP_DOWNLOARD:
		if (0U == XCP_Connected)
		{
			XCP_SendError(XCP_ERR_ACCESS_LOCKED);
			break;
		}
		XCP_CmdDownload(data);
		break;

	case (uint8_t)XCP_SHORTUPLOAD:
		if (0U == XCP_Connected)
		{
			XCP_SendError(XCP_ERR_ACCESS_LOCKED);
			break;
		}
		XCP_CmdShortUpload(data);
		break;

	default:
		XCP_SendError(XCP_ERR_CMD_UNKNOWN);
		break;
	}
}

#endif /* XCP_ENABLE */
