/*
 * BSW_CanCallback.c
 *
 *  Created on: 2026年10月22日
 *      Author: CENSORED
 */


#include "ASW_CanCallback.h"
#include "BSW_CanCallback.h"
#include "can_fd_message.h"
#include "Can_43_FLEXCAN.h"
#include "Can_43_FLEXCAN_Externals.h"
#include "SchM_Can_43_FLEXCAN.h"
#include "UserOS.h"

#define BSW_CANRX_CHANNEL_COUNT 1U

typedef struct
{
    CAN_RXStr *list;
    unsigned int count;
} BSW_CANRX_ChannelType;

static const BSW_CANRX_ChannelType BSW_CANRX_Channels[BSW_CANRX_CHANNEL_COUNT] =
{
    { CAN_CHN1_RX, CAN_CHN1_RX_COUNT }
};

#define BSW_CANTX_CHANNEL_COUNT 1U
#define BSW_CAN_BUSOFF_RECOVERY_MS 100U

typedef struct
{
    CAN_TXStr *list;
    unsigned int count;
    unsigned short hth;
} BSW_CANTX_ChannelType;

static const BSW_CANTX_ChannelType BSW_CANTX_Channels[BSW_CANTX_CHANNEL_COUNT] =
{
    { CAN_CHN1_TX, CAN_CHN1_TX_COUNT, 1U }
};

static void BSW_CANRX_Swap(CAN_RXStr* lhs, CAN_RXStr* rhs)
{
    CAN_RXStr temp = *lhs;
    *lhs = *rhs;
    *rhs = temp;
}

static void BSW_CANRX_Heapify(CAN_RXStr* str, unsigned int count, unsigned int root)
{
    unsigned int largest;
    unsigned int left;
    unsigned int right;

    for (;;)
    {
        largest = root;
        left = (root << 1U) + 1U;
        right = left + 1U;

        if ((left < count) && (str[left].canid > str[largest].canid))
        {
            largest = left;
        }

        if ((right < count) && (str[right].canid > str[largest].canid))
        {
            largest = right;
        }

        if (largest == root)
        {
            break;
        }

        BSW_CANRX_Swap(&str[root], &str[largest]);
        root = largest;
    }
}

void BSW_CANRX_INIT(CAN_RXStr* str, unsigned char len)
{
    unsigned int count = (unsigned int)len;
    unsigned int index;
    unsigned int end;

    if ((NULL_PTR == str) || (count < 2U))
    {
        return;
    }

    /* Build a max-heap in place. */
    index = count / 2U;
    while (index > 0U)
    {
        index--;
        BSW_CANRX_Heapify(str, count, index);
    }

    /* Move the maximum to the end repeatedly, producing ascending order. */
    end = count;
    while (end > 1U)
    {
        end--;
        BSW_CANRX_Swap(&str[0], &str[end]);
        BSW_CANRX_Heapify(str, end, 0U);
    }
}

static CAN_RXStr* BSW_CANRX_Find(CAN_RXStr* list, unsigned int count, unsigned int canid)
{
    unsigned int low = 0U;
    unsigned int high = count;

    while (low < high)
    {
        unsigned int mid = low + ((high - low) >> 1U);

        if (list[mid].canid < canid)
        {
            low = mid + 1U;
        }
        else
        {
            high = mid;
        }
    }

    if ((low < count) && (list[low].canid == canid))
    {
        return &list[low];
    }

    return NULL_PTR;
}

static unsigned char BSW_CANRX_LengthToDlc(unsigned char length)
{
    if (length <= 8U)
    {
        return length;
    }
    if (length <= 12U)
    {
        return 9U;
    }
    if (length <= 16U)
    {
        return 10U;
    }
    if (length <= 20U)
    {
        return 11U;
    }
    if (length <= 24U)
    {
        return 12U;
    }
    if (length <= 32U)
    {
        return 13U;
    }
    if (length <= 48U)
    {
        return 14U;
    }

    return 15U;
}

static void BSW_CANRX_FillMessage
(
    CAN_FD_MESSAGE* message,
    Can_IdType canId,
    unsigned char length,
    const unsigned char* data,
    unsigned int now
)
{
    unsigned int index;
    unsigned int copyLength = (unsigned int)length;

    if (copyLength > (unsigned int)sizeof(message->Data))
    {
        copyLength = (unsigned int)sizeof(message->Data);
    }

    message->ProtocolMode = ((canId & 0x40000000U) != 0U) ? 1U : 0U;
    message->Extended = ((canId & 0x80000000U) != 0U) ? 1U : 0U;
    message->Length = length;
    message->Remote = 0U;
    message->Error = 0U;
    message->BRS = 0U;
    message->ESI = 0U;
    message->DLC = BSW_CANRX_LengthToDlc(length);
    message->ID = ((uint32_T)canId) & 0x1FFFFFFFUL;
    message->Reserved = 0U;
    message->Timestamp = (double)now;

    for (index = 0U; index < (unsigned int)sizeof(message->Data); index++)
    {
        message->Data[index] = 0U;
    }

    if (NULL_PTR != data)
    {
        for (index = 0U; index < copyLength; index++)
        {
            message->Data[index] = data[index];
        }
    }
}

uint32 BSW_cntCan0RxCnt = 0;
boolean BSW_CanRxCallout
(
    uint8 Hrh,
    Can_IdType CanId,
    uint8 CanDataLegth,
    const uint8* CanSduPtr
)
{
    const BSW_CANRX_ChannelType* channel;
    CAN_RXStr* rxEntry;
    unsigned int canid;
    unsigned int now;

    if (Hrh >= BSW_CANRX_CHANNEL_COUNT)
    {
        return FALSE;
    }

    channel = &BSW_CANRX_Channels[Hrh];
    if ((NULL_PTR == channel->list) || (0U == channel->count))
    {
        return FALSE;
    }

    canid = ((unsigned int)CanId) & 0x1FFFFFFFUL;
    rxEntry = BSW_CANRX_Find(channel->list, channel->count, canid);
    if (NULL_PTR == rxEntry)
    {
        return FALSE;
    }

    now = BSW_GetSystemTick();
    rxEntry->get_time_tick = now;
    BSW_cntCan0RxCnt++;

    if ((NULL_PTR != rxEntry->timeout_flag) && (0U != *(rxEntry->timeout_flag)))
    {
        *(rxEntry->timeout_flag) = 0U;
    }

    if (NULL_PTR != rxEntry->Data)
    {
        BSW_CANRX_FillMessage(rxEntry->Data, CanId, (unsigned char)CanDataLegth, CanSduPtr, now);
    }

    if (NULL_PTR != rxEntry->Callback)
    {
        rxEntry->Callback(rxEntry->Data);
    }

    return FALSE;
}

void BSW_CANRX_TIMEOUT_POLL(void)
{
    unsigned int now = BSW_GetSystemTick();
    unsigned int channelIndex;
    unsigned int entryIndex;

    for (channelIndex = 0U; channelIndex < BSW_CANRX_CHANNEL_COUNT; channelIndex++)
    {
        const BSW_CANRX_ChannelType* channel = &BSW_CANRX_Channels[channelIndex];

        if ((NULL_PTR == channel->list) || (0U == channel->count))
        {
            continue;
        }

        for (entryIndex = 0U; entryIndex < channel->count; entryIndex++)
        {
            CAN_RXStr* rxEntry = &channel->list[entryIndex];

            if ((NULL_PTR != rxEntry->timeout_flag) && (0U != rxEntry->overtime))
            {
                if ((now - rxEntry->get_time_tick) >= rxEntry->overtime)
                {
                    *(rxEntry->timeout_flag) = 1U;
                }
            }
        }
    }
}

void BSW_CANTX_POLL(void)
{
    unsigned int now = BSW_GetSystemTick();
    unsigned int channelIndex;
    unsigned int entryIndex;
    Can_ControllerStateType controllerMode = CAN_CS_UNINIT;

    if ((E_OK != Can_43_FLEXCAN_GetControllerMode(0U, &controllerMode)) ||
        (CAN_CS_STARTED != controllerMode))
    {
        return;
    }

    for (channelIndex = 0U; channelIndex < BSW_CANTX_CHANNEL_COUNT; channelIndex++)
    {
        const BSW_CANTX_ChannelType* channel = &BSW_CANTX_Channels[channelIndex];

        if ((NULL_PTR == channel->list) || (0U == channel->count))
        {
            continue;
        }

        for (entryIndex = 0U; entryIndex < channel->count; entryIndex++)
        {
            CAN_TXStr* txEntry = &channel->list[entryIndex];
            CAN_TX_BufType txBuffer;
            Can_PduType pdu;
            Can_IdType canId;
            unsigned char length;

            if ((NULL_PTR == txEntry->Data) ||
                (0U == txEntry->Enable) ||
                (0U == txEntry->send_period))
            {
                continue;
            }

            if ((now - txEntry->last_send_tick) < txEntry->send_period)
            {
                continue;
            }

            /* Work on a local buffer so the callback may modify the frame. */
            txBuffer.Message = *(txEntry->Data);

            if (NULL_PTR != txEntry->Callback)
            {
                txEntry->Callback(&txBuffer);
            }

            length = txBuffer.Message.Length;
            if (length > (unsigned char)sizeof(txBuffer.Message.Data))
            {
                length = (unsigned char)sizeof(txBuffer.Message.Data);
            }

            canId = (Can_IdType)txBuffer.Message.ID;
            if (0U != txBuffer.Message.Extended)
            {
                canId |= 0x80000000UL;
            }
            if (0U != txBuffer.Message.ProtocolMode)
            {
                canId |= 0x40000000UL;
            }

            pdu.id = canId;
            pdu.length = length;
            pdu.sdu = txBuffer.Message.Data;
            pdu.swPduHandle = 0U;

            if (E_OK == Can_43_FLEXCAN_Write((Can_HwHandleType)channel->hth, &pdu))
            {
                txEntry->last_send_tick = now;
            }
        }
    }
}

void BSW_CAN_BUSOFF_POLL(void)
{
    static unsigned int busoffStartTick = 0U;
    static unsigned char busoffPending = 0U;
    Can_ControllerStateType controllerMode = CAN_CS_UNINIT;
    unsigned int now = BSW_GetSystemTick();

    /* Clear the bus-off flag and let the CAN driver update its controller state. */
    Can_43_FLEXCAN_MainFunction_BusOff();

    if (E_OK != Can_43_FLEXCAN_GetControllerMode(0U, &controllerMode))
    {
        return;
    }

    if (CAN_CS_STOPPED == controllerMode)
    {
        if (0U == busoffPending)
        {
            busoffPending = 1U;
            busoffStartTick = now;
        }
        else if ((now - busoffStartTick) >= BSW_CAN_BUSOFF_RECOVERY_MS)
        {
            if (E_OK == Can_43_FLEXCAN_SetControllerMode(0U, CAN_CS_STARTED))
            {
                busoffPending = 0U;
            }
            else
            {
                busoffStartTick = now;
            }
        }
    }
    else
    {
        busoffPending = 0U;
    }
}
