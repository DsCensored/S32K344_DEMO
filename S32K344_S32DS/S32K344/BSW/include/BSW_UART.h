/*
 * BSW_UART.h
 *
 *  Created on: 2026年10月27日
 *      Author: CENSORED
 *
 * UART6 (LPUART6) BSW 接口：
 *   - 接收：应用层周期调用 BSW_UART6_RxPoll()，一帧接收完成后数据被拷贝到
 *           BSW_rUART6RxBuf，长度放在 BSW_u16UART6RxLen；
 *   - 发送：调用 BSW_UART6_Send(data, len)，内部拷贝到 BSW_rUART6TxBuf 后由 DMA 发出。
 */

#ifndef INCLUDE_BSW_UART_H_
#define INCLUDE_BSW_UART_H_

#include "Std_Types.h"
#include "Uart_Ipw_Types.h"

/* Uart 驱动通道号：Uart_xConfig 只配置了通道 0，对应硬件 LPUART6 */
#define BSW_UART6_CHANNEL               (0U)

/* 收发缓冲区长度（TBD） */
#define BSW_UART6_BUF_SIZE              (1024U)

/* 接收空闲判定：连续 N 次轮询 DMA 剩余字节数不变，就认为一帧结束。
 * TODO: TBD —— 需要和 BSW_UART6_RxPoll() 的调用周期一起调整
 *       （当前挂在 ASW 的 2 ms 任务里，5 次 ≈ 10 ms 空闲） */
#define BSW_UART6_RX_IDLE_POLLS         (5U)

/* 发送完成依赖 DMA 通道 16 中断（DMATCD16），已在 IntCtrl_Ip_Cfg.c 里使能；
 * 发送期间不能修改 BSW_rUART6TxBuf，用 BSW_UART6_IsTxBusy() 判断是否发完。 */

/* 应用层接收缓冲区：收到完整一帧后由 BSW_UART6_RxPoll() 写入 */
extern uint8 BSW_rUART6RxBuf[BSW_UART6_BUF_SIZE];

/* 发送暂存缓冲区：BSW_UART6_Send() 内部 memcpy 到这里，DMA 从这里取数据发送 */
extern uint8 BSW_rUART6TxBuf[BSW_UART6_BUF_SIZE];

/* 最近一帧的接收长度 / 最近一次发送的长度（TBD：长度是否也做成 2048） */
extern volatile uint16 BSW_u16UART6RxLen;
extern volatile uint16 BSW_u16UART6TxLen;

/* 累计收到的完整帧数（调试用：在 Watch 窗口里看它有没有在涨） */
extern volatile uint32 BSW_u32UART6RxFrameCnt;

/* 启动 DMA 接收失败的次数（调试用：>0 说明 Uart_AsyncReceive 没起来） */
extern volatile uint32 BSW_u32UART6RxStartErrCnt;

/* 初始化：启动第一次 DMA 接收（在 Uart_Init() 之后调用） */
void BSW_UART6_Init(void);

/* 接收轮询：周期调用（例如 2 ms），完成一帧后拷贝到 BSW_rUART6RxBuf，
 * 并自动重新启动下一次接收。 */
void BSW_UART6_RxPoll(void);

/* 发送 data 中的 Length 个字节：先 memcpy 到 BSW_rUART6TxBuf，再由 DMA 异步发送，立即返回。
 * 返回 E_OK 表示 DMA 已启动；上一帧没发完时返回 E_NOT_OK。
 * 发送期间不要调用本函数重新发送；可用 BSW_UART6_IsTxBusy() 查询是否发完，
 * 或用 Uart 回调的 UART_EVENT_TX_EMPTY。 */
Std_ReturnType BSW_UART6_Send(uint8* data, uint16 Length);

/* 上一次 DMA 发送是否还在进行中 */
boolean BSW_UART6_IsTxBusy(void);

/* 是否有新的接收帧（应用取走数据后调用 BSW_UART6_ReleaseRxFrame() 清除） */
boolean BSW_UART6_IsRxFrameReady(void);
void BSW_UART6_ReleaseRxFrame(void);

/* 当前这一帧已经收到、但还没判定成帧的字节数（调试用，只读，不影响接收） */
uint16 BSW_UART6_GetRxPending(void);

/* Uart 事件回调（当前 EB 配置未使能，预留；见 BSW_UART.c 说明） */
void BSW_UARTCallback(uint8 Channel, Uart_EventType Event);

#endif /* INCLUDE_BSW_UART_H_ */
