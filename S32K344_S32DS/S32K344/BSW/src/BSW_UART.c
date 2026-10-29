/*
 * BSW_UART.c
 *
 *  Created on: 2026年10月27日
 *      Author: CENSORED
 *
 * UART6 (LPUART6) 收发实现（DMA + 轮询）
 *
 * 说明：
 *   - DMA 直接写入内部暂存区 BSW_rUART6RxDmaBuf，BSW_UART6_RxPoll() 判定一帧结束后
 *     再按实际长度拷贝到应用可见的 BSW_rUART6RxBuf，避免应用读到半帧；
 *   - 由于 Uart 驱动只有“收满 BufferSize”才更新完成状态，这里用“空闲判定”收短帧：
 *     连续 BSW_UART6_RX_IDLE_POLLS 次轮询 DMA 剩余字节数不变，即认为一帧结束；
 *   - 当前 EB 生成的 Uart 配置中没有使能回调（Uart_Ipw_xHwConfigPB_0.Callback = NULL_PTR，
 *     UART_CALLBACK_DECLARED = STD_OFF），所以 BSW_UARTCallback() 不会被执行，
 *     全部逻辑走轮询。
 */

#include "BSW_UART.h"
#include "CDD_Uart.h"

#include <string.h>

/*------------------------------------------------------------------------------------------------*/
/* 应用层收发缓冲区                                                                                */
/*------------------------------------------------------------------------------------------------*/
uint8 BSW_rUART6RxBuf[BSW_UART6_BUF_SIZE];
uint8 BSW_rUART6TxBuf[BSW_UART6_BUF_SIZE];

volatile uint16 BSW_u16UART6RxLen = 0U;
volatile uint16 BSW_u16UART6TxLen = 0U;
volatile uint32 BSW_u32UART6RxFrameCnt = 0U;
volatile uint32 BSW_u32UART6RxStartErrCnt = 0U;

/*------------------------------------------------------------------------------------------------*/
/* 内部状态                                                                                        */
/*------------------------------------------------------------------------------------------------*/
/* DMA 目标缓冲区：必须一直有效，直到本次接收结束 */
static uint8 BSW_rUART6RxDmaBuf[BSW_UART6_BUF_SIZE];

static volatile boolean BSW_flgUART6RxStarted    = FALSE;
static volatile boolean BSW_flgUART6RxFrameReady = FALSE;
static volatile uint32  BSW_u32UART6LastRemaining = BSW_UART6_BUF_SIZE;
static volatile uint16  BSW_u16UART6IdlePolls     = 0U;

/*------------------------------------------------------------------------------------------------*/
/* 内部函数                                                                                        */
/*------------------------------------------------------------------------------------------------*/
static void BSW_UART6_StartRx(void)
{
    if (E_OK == Uart_AsyncReceive(BSW_UART6_CHANNEL, BSW_rUART6RxDmaBuf, BSW_UART6_BUF_SIZE))
    {
        BSW_flgUART6RxStarted      = TRUE;
        BSW_u32UART6LastRemaining  = BSW_UART6_BUF_SIZE;
        BSW_u16UART6IdlePolls      = 0U;
    }
    else
    {
        /* 启动失败（上一次还没结束等），下一次 RxPoll 再重试 */
        BSW_u32UART6RxStartErrCnt++;
        BSW_flgUART6RxStarted = FALSE;
    }
}

static void BSW_UART6_StoreRxFrame(uint32 Length)
{
    if (Length > BSW_UART6_BUF_SIZE)
    {
        Length = BSW_UART6_BUF_SIZE;
    }

    if (0U != Length)
    {
        (void)memcpy(BSW_rUART6RxBuf, BSW_rUART6RxDmaBuf, Length);
        BSW_u16UART6RxLen        = (uint16)Length;
        BSW_u32UART6RxFrameCnt++;
        BSW_flgUART6RxFrameReady = TRUE;
    }
}

/*------------------------------------------------------------------------------------------------*/
/* 对外接口                                                                                        */
/*------------------------------------------------------------------------------------------------*/
void BSW_UART6_Init(void)
{
    BSW_u16UART6RxLen        = 0U;
    BSW_u16UART6TxLen        = 0U;
    BSW_u32UART6RxFrameCnt    = 0U;
    BSW_u32UART6RxStartErrCnt = 0U;
    BSW_flgUART6RxFrameReady  = FALSE;
    BSW_flgUART6RxStarted    = FALSE;

    /* 启动第一次 DMA 接收 */
    BSW_UART6_StartRx();
}

void BSW_UART6_RxPoll(void)
{
    /* 初值给满：即使 DMA 查询失败也不会被误判成“收满一帧” */
    uint32          remaining = BSW_UART6_BUF_SIZE;
    uint32          received;
    Uart_StatusType status;

    /* 还没开始（或上次启动失败）就先启动接收 */
    if (FALSE == BSW_flgUART6RxStarted)
    {
        BSW_UART6_StartRx();
        return;
    }

    status = Uart_GetStatus(BSW_UART6_CHANNEL, &remaining, UART_RECEIVE);

    if (UART_STATUS_OPERATION_ONGOING == status)
    {
        received = (remaining <= BSW_UART6_BUF_SIZE) ? (BSW_UART6_BUF_SIZE - remaining) : 0U;

        if (remaining != BSW_u32UART6LastRemaining)
        {
            /* 还在收数据，重新计时 */
            BSW_u32UART6LastRemaining = remaining;
            BSW_u16UART6IdlePolls     = 0U;
        }
        else if (received > 0U)
        {
            /* 有数据但一段时间没有新数据：认为一帧结束 */
            BSW_u16UART6IdlePolls++;
            if (BSW_u16UART6IdlePolls >= BSW_UART6_RX_IDLE_POLLS)
            {
                /* 先中止本次 DMA，再取已接收的字节数 */
                (void)Uart_Abort(BSW_UART6_CHANNEL, UART_RECEIVE);

                BSW_UART6_StoreRxFrame(received);

                /* 重新开始下一帧 */
                BSW_flgUART6RxStarted = FALSE;
                BSW_UART6_StartRx();
            }
        }
        else
        {
            BSW_u16UART6IdlePolls = 0U;
        }
    }
    else if (UART_STATUS_NO_ERROR == status)
    {
        /* 缓冲区被收满：整帧有效 */
        BSW_UART6_StoreRxFrame(BSW_UART6_BUF_SIZE);

        BSW_flgUART6RxStarted = FALSE;
        BSW_UART6_StartRx();
    }
    else
    {
        /* 帧错误 / 溢出 / 被中止：丢掉本帧，重新开始接收 */
        (void)Uart_Abort(BSW_UART6_CHANNEL, UART_RECEIVE);

        BSW_flgUART6RxStarted = FALSE;
        BSW_UART6_StartRx();
    }
}

Std_ReturnType BSW_UART6_Send(uint8* data,uint16 Length)
{
    Std_ReturnType RetVal;

    if ((0U == Length) || (Length > BSW_UART6_BUF_SIZE) || BSW_UART6_IsTxBusy() == TRUE || data == NULL)
    {
        return E_NOT_OK;
    }

    memcpy(BSW_rUART6TxBuf, data, Length);

    /* DMA 发送：立即返回。TransferType 已配置为 LPUART_UART_IP_USING_DMA。
     * 上一帧没发完时 Uart_AsyncSend 会返回 E_NOT_OK。 */
    RetVal = Uart_AsyncSend(BSW_UART6_CHANNEL, BSW_rUART6TxBuf, Length);
    if (E_OK == RetVal)
    {
        BSW_u16UART6TxLen = Length;
    }

    return RetVal;
}

boolean BSW_UART6_IsTxBusy(void)
{
    uint32 remaining = 0U;

    return (UART_STATUS_OPERATION_ONGOING == Uart_GetStatus(BSW_UART6_CHANNEL, &remaining, UART_SEND))
           ? TRUE : FALSE;
}

boolean BSW_UART6_IsRxFrameReady(void)
{
    return BSW_flgUART6RxFrameReady;
}

void BSW_UART6_ReleaseRxFrame(void)
{
    BSW_flgUART6RxFrameReady = FALSE;
}

uint16 BSW_UART6_GetRxPending(void)
{
    uint32 remaining = BSW_UART6_BUF_SIZE;

    /* 只读查询，不影响正在进行的接收 */
    if (UART_STATUS_OPERATION_ONGOING == Uart_GetStatus(BSW_UART6_CHANNEL, &remaining, UART_RECEIVE))
    {
        if (remaining <= BSW_UART6_BUF_SIZE)
        {
            return (uint16)(BSW_UART6_BUF_SIZE - remaining);
        }
    }

    return 0U;
}

/*
 * Uart 事件回调。
 * 注意：当前 EB 配置中 Uart 回调未使能（Uart_Ipw_xHwConfigPB_0.Callback = NULL_PTR），
 *       该函数不会被调用；真正的收帧处理在 BSW_UART6_RxPoll() 里。
 *       以后如果在 EB 里把回调配置成 BSW_UARTCallback，Channel 参数是 Uart 通道号（0），
 *       不是硬件实例号 6。
 */
void BSW_UARTCallback(uint8 Channel, Uart_EventType Event)
{
    if (BSW_UART6_CHANNEL != Channel)
    {
        return;
    }

    switch (Event)
    {
        case UART_EVENT_ERROR:
            /* TODO: 需要的话在这里记录错误，或在下次 RxPoll 里做恢复 */
            break;

        case UART_EVENT_RX_FULL:
        case UART_EVENT_TX_EMPTY:
        case UART_EVENT_END_TRANSFER:
        default:
            /* 由 BSW_UART6_RxPoll() 统一处理 */
            break;
    }
}
