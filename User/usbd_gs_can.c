#include <string.h>
#include "usbd_gs_can.h"
#include "descriptors.h"
#include "m253_canfd.h"
#include "timestamp.h"
#include "led.h"
#include "debug_log.h"

/* USB Control EP0 Buffer */
static uint8_t s_ep0_rx_buf[64] __aligned(4);
static uint8_t s_ep0_tx_buf[128] __aligned(4);
static uint8_t s_u8PendingVendorReq = 0xFF;

/* Ring Buffers */
static struct gs_host_frame s_tx_ring[CAN_TX_RING_SIZE] __aligned(4);
static volatile uint32_t s_tx_head = 0;
static volatile uint32_t s_tx_tail = 0;

static struct gs_host_frame s_rx_ring[CAN_RX_RING_SIZE] __aligned(4);
static volatile uint32_t s_rx_head = 0;
static volatile uint32_t s_rx_tail = 0;

/* Bulk IN (EP2) Slicing State Machine */
static volatile bool s_bEp2Busy = false;
static uint8_t s_in_active_buf[80] __aligned(4);
static volatile uint32_t s_in_total_len = 0;
static volatile uint32_t s_in_sent_len = 0;

/* Bulk OUT (EP3) Assembly State Machine */
static uint8_t s_out_assembly_buf[80] __aligned(4);
static volatile uint32_t s_out_assembly_len = 0;
static volatile uint32_t s_u32OutToggle = 0xFFFFFFFFUL;

/* Global Statistics Instance */
traffic_stats_t g_stats = {0};

/* Ring Buffer Helpers */
bool usbd_gs_can_enqueue_in_frame(const struct gs_host_frame *frame)
{
    uint32_t next_head = (s_rx_head + 1) % CAN_RX_RING_SIZE;
    if (next_head == s_rx_tail)
    {
        return false; /* Queue full */
    }

    memcpy(&s_rx_ring[s_rx_head], frame, sizeof(struct gs_host_frame));
    s_rx_head = next_head;
    led_activity_trigger();
    return true;
}

static bool dequeue_in_frame(struct gs_host_frame *frame)
{
    if (s_rx_head == s_rx_tail)
    {
        return false; /* Queue empty */
    }

    memcpy(frame, &s_rx_ring[s_rx_tail], sizeof(struct gs_host_frame));
    s_rx_tail = (s_rx_tail + 1) % CAN_RX_RING_SIZE;
    return true;
}

static bool enqueue_out_frame(const struct gs_host_frame *frame)
{
    uint32_t next_head = (s_tx_head + 1) % CAN_TX_RING_SIZE;
    if (next_head == s_tx_tail)
    {
        return false; /* Queue full */
    }

    memcpy(&s_tx_ring[s_tx_head], frame, sizeof(struct gs_host_frame));
    s_tx_head = next_head;
    led_activity_trigger();
    return true;
}

bool usbd_gs_can_dequeue_out_frame(struct gs_host_frame *frame)
{
    if (s_tx_head == s_tx_tail)
    {
        return false; /* Queue empty */
    }

    memcpy(frame, &s_tx_ring[s_tx_tail], sizeof(struct gs_host_frame));
    s_tx_tail = (s_tx_tail + 1) % CAN_TX_RING_SIZE;
    return true;
}

/* Endpoint Hardware Initialization */
void usbd_gs_can_init(void)
{
    /* Init Setup packet buffer base [0x000 ~ 0x007] */
    USBD->STBUFSEG = SETUP_BUF_BASE;

    /* EP0 ==> Control IN endpoint, address 0 */
    USBD_CONFIG_EP(EP0, USBD_CFG_CSTALL | USBD_CFG_EPMODE_IN | 0);
    USBD_SET_EP_BUF_ADDR(EP0, EP0_BUF_BASE);

    /* EP1 ==> Control OUT endpoint, address 0 */
    USBD_CONFIG_EP(EP1, USBD_CFG_CSTALL | USBD_CFG_EPMODE_OUT | 0);
    USBD_SET_EP_BUF_ADDR(EP1, EP1_BUF_BASE);

    /* EP2 ==> Bulk IN endpoint, address 1 (0x81) */
    USBD_CONFIG_EP(EP2, USBD_CFG_EPMODE_IN | 1);
    USBD_SET_EP_BUF_ADDR(EP2, EP2_BUF_BASE);

    /* EP3 ==> Bulk OUT endpoint, address 2 (0x02) */
    USBD_CONFIG_EP(EP3, USBD_CFG_EPMODE_OUT | 2);
    USBD_SET_EP_BUF_ADDR(EP3, EP3_BUF_BASE);

    /* Trigger EP3 to receive initial OUT data */
    USBD_SET_PAYLOAD_LEN(EP3, EP3_MAX_PKT_SIZE);

    s_bEp2Busy = false;
    s_in_total_len = 0;
    s_in_sent_len = 0;
    s_out_assembly_len = 0;
    s_u32OutToggle = 0xFFFFFFFFUL;
}

/* Class Request (gs_usb uses vendor requests, standard class request can STALL) */
void usbd_gs_can_class_request(void)
{
    USBD_SET_EP_STALL(EP0);
    USBD_SET_EP_STALL(EP1);
}

/* RAM Trace Buffer for Debugging */
volatile usb_trace_t g_usb_trace[16];
volatile uint8_t g_usb_trace_idx = 0;

/* Vendor Request Handler (EP0) */
void usbd_gs_can_vendor_request(void)
{
    uint8_t req_type = g_USBD_au8SetupPacket[0];
    uint8_t req_code = g_USBD_au8SetupPacket[1];
    uint16_t wValue  = (uint16_t)(g_USBD_au8SetupPacket[2] | (g_USBD_au8SetupPacket[3] << 8));
    uint16_t wIndex  = (uint16_t)(g_USBD_au8SetupPacket[4] | (g_USBD_au8SetupPacket[5] << 8));
    uint16_t wLength = (uint16_t)(g_USBD_au8SetupPacket[6] | (g_USBD_au8SetupPacket[7] << 8));

    if (g_usb_trace_idx < 16)
    {
        g_usb_trace[g_usb_trace_idx].req_type = req_type;
        g_usb_trace[g_usb_trace_idx].req_code = req_code;
        g_usb_trace[g_usb_trace_idx].wValue = wValue;
        g_usb_trace[g_usb_trace_idx].wIndex = wIndex;
        g_usb_trace[g_usb_trace_idx].wLength = wLength;
        g_usb_trace_idx++;
    }
    (void)wValue;

    /* 1. Microsoft WCID 1.0 (WinUSB) Requests */
    if (req_code == USBD_WCID_VENDOR_CODE)
    {
        if (wIndex == 0x0004)
        {
            uint32_t len = sizeof(gu8WcidCompatibleIdDesc);
            if (len > wLength) len = wLength;
            USBD_PrepareCtrlIn((uint8_t *)gu8WcidCompatibleIdDesc, len);
            USBD_PrepareCtrlOut(0, 0UL);
            LOG_DEBUG("[USB] WCID: Compatible ID queried\r\n");
            return;
        }
        else if (wIndex == 0x0005)
        {
            uint32_t len = sizeof(gu8WcidExtPropertyDesc);
            if (len > wLength) len = wLength;
            USBD_PrepareCtrlIn((uint8_t *)gu8WcidExtPropertyDesc, len);
            USBD_PrepareCtrlOut(0, 0UL);
            LOG_DEBUG("[USB] WCID: Ext Property GUID queried\r\n");
            return;
        }
    }

    /* 2. gs_usb Protocol Commands */
    if (req_type & 0x80)
    {
        /* Device to Host (IN) */
        switch (req_code)
        {
            case GS_USB_BREQ_DEVICE_CONFIG:
            {
                struct gs_device_config dev_cfg;
                memset(&dev_cfg, 0, sizeof(dev_cfg));
                dev_cfg.icount = 0;         /* 1 channel (channel 0) */
                dev_cfg.sw_version = 2;     /* Protocol version 2 */
                dev_cfg.hw_version = 1;     /* HW version 1 */

                uint32_t len = sizeof(dev_cfg);
                if (len > wLength) len = wLength;
                memcpy(s_ep0_tx_buf, &dev_cfg, len);
                USBD_PrepareCtrlIn(s_ep0_tx_buf, len);
                USBD_PrepareCtrlOut(0, 0UL);
                return;
            }

            case GS_USB_BREQ_BT_CONST:
            {
                uint32_t len = sizeof(g_can_bt_const);
                if (len > wLength) len = wLength;
                memcpy(s_ep0_tx_buf, &g_can_bt_const, len);
                USBD_PrepareCtrlIn(s_ep0_tx_buf, len);
                USBD_PrepareCtrlOut(0, 0UL);
                return;
            }

            case GS_USB_BREQ_BT_CONST_EXT:
            {
                uint32_t len = sizeof(g_can_bt_const_ext);
                if (len > wLength) len = wLength;
                memcpy(s_ep0_tx_buf, &g_can_bt_const_ext, len);
                USBD_PrepareCtrlIn(s_ep0_tx_buf, len);
                USBD_PrepareCtrlOut(0, 0UL);
                return;
            }

            case GS_USB_BREQ_TIMESTAMP:
            {
                uint32_t ts = timer_get_us();
                uint32_t len = sizeof(ts);
                if (len > wLength) len = wLength;
                memcpy(s_ep0_tx_buf, &ts, len);
                USBD_PrepareCtrlIn(s_ep0_tx_buf, len);
                USBD_PrepareCtrlOut(0, 0UL);
                return;
            }

            case GS_USB_BREQ_GET_TERMINATION:
            {
                struct gs_device_termination_state term;
                term.state = GS_CAN_TERMINATION_STATE_OFF;
                uint32_t len = sizeof(term);
                if (len > wLength) len = wLength;
                memcpy(s_ep0_tx_buf, &term, len);
                USBD_PrepareCtrlIn(s_ep0_tx_buf, len);
                USBD_PrepareCtrlOut(0, 0UL);
                return;
            }

            case GS_USB_BREQ_GET_STATE:
            {
                struct gs_device_state state;
                m253_canfd_get_state(&state);
                uint32_t len = sizeof(state);
                if (len > wLength) len = wLength;
                memcpy(s_ep0_tx_buf, &state, len);
                USBD_PrepareCtrlIn(s_ep0_tx_buf, len);
                USBD_PrepareCtrlOut(0, 0UL);
                return;
            }

            default:
                break;
        }
    }
    else
    {
        /* Host to Device (OUT) */
        s_u8PendingVendorReq = req_code;

        if (wLength > 0 && wLength <= sizeof(s_ep0_rx_buf))
        {
            USBD_SET_DATA1(EP1);
            USBD_PrepareCtrlOut(s_ep0_rx_buf, wLength);
            /* Status stage: arm EP0 for IN token */
            USBD_SET_DATA1(EP0);
            USBD_SET_PAYLOAD_LEN(EP0, 0UL);
            return;
        }
        else
        {
            /* Zero-length request */
            USBD_SET_DATA1(EP0);
            USBD_SET_PAYLOAD_LEN(EP0, 0UL);
            return;
        }
    }

    /* Unhandled vendor request -> STALL */
    USBD_SET_EP_STALL(EP0);
    USBD_SET_EP_STALL(EP1);
}

/* Control OUT Stage Completed */
void usbd_gs_can_ctrl_out_done(void)
{
    if (s_u8PendingVendorReq == 0xFF)
    {
        return;
    }

    switch (s_u8PendingVendorReq)
    {
        case GS_USB_BREQ_HOST_FORMAT:
        {
            break;
        }

        case GS_USB_BREQ_BITTIMING:
        {
            m253_canfd_set_bittiming((const struct gs_device_bittiming *)s_ep0_rx_buf);
            break;
        }

        case GS_USB_BREQ_DATA_BITTIMING:
        {
            m253_canfd_set_data_bittiming((const struct gs_device_bittiming *)s_ep0_rx_buf);
            break;
        }

        case GS_USB_BREQ_MODE:
        {
            const struct gs_device_mode *mode = (const struct gs_device_mode *)s_ep0_rx_buf;
            m253_canfd_set_mode(mode->mode, mode->flags);
            led_set_channel_active(mode->mode == GS_CAN_MODE_START);
            if (mode->mode == GS_CAN_MODE_RESET)
            {
                s_tx_head = 0;
                s_tx_tail = 0;
                s_rx_head = 0;
                s_rx_tail = 0;
                s_out_assembly_len = 0;
            }
            break;
        }

        case GS_USB_BREQ_IDENTIFY:
        {
            const struct gs_identify_mode *imode = (const struct gs_identify_mode *)s_ep0_rx_buf;
            led_set_identify(imode->mode != 0);
            break;
        }

        default:
            break;
    }

    s_u8PendingVendorReq = 0xFF;
}

/* Bulk IN (EP2) Handler: Sends Chunks to Host */
void usbd_gs_can_ep2_in_handler(void)
{
    if (s_in_sent_len < s_in_total_len)
    {
        /* Send remainder chunk (Packet 2: e.g. 16 bytes) */
        uint32_t chunk_len = s_in_total_len - s_in_sent_len;
        if (chunk_len > EP2_MAX_PKT_SIZE) chunk_len = EP2_MAX_PKT_SIZE;

        USBD_MemCopy((uint8_t *)(USBD_BUF_BASE + USBD_GET_EP_BUF_ADDR(EP2)),
                     &s_in_active_buf[s_in_sent_len],
                     chunk_len);

        s_in_sent_len += chunk_len;
        USBD_SET_PAYLOAD_LEN(EP2, chunk_len);
        g_stats.usb_chunk_in_count++;
    }
    else
    {
        /* Full multi-packet transfer completed */
        s_bEp2Busy = false;
        s_in_total_len = 0;
        s_in_sent_len = 0;
        g_stats.usb_in_count++;
    }
}

/* Bulk OUT (EP3) Handler: Receives & Reassembles Chunks from Host */
void usbd_gs_can_ep3_out_handler(void)
{
    if (s_u32OutToggle == (USBD->EPSTS0 & USBD_EPSTS0_EPSTS3_Msk))
    {
        USBD_SET_PAYLOAD_LEN(EP3, EP3_MAX_PKT_SIZE);
        return;
    }
    s_u32OutToggle = USBD->EPSTS0 & USBD_EPSTS0_EPSTS3_Msk;

    uint32_t rx_len = USBD_GET_PAYLOAD_LEN(EP3);
    uint8_t *pData = (uint8_t *)(USBD_BUF_BASE + USBD_GET_EP_BUF_ADDR(EP3));

    if (rx_len > 0 && (s_out_assembly_len + rx_len) <= sizeof(s_out_assembly_buf))
    {
        memcpy(&s_out_assembly_buf[s_out_assembly_len], pData, rx_len);
        s_out_assembly_len += rx_len;
        g_stats.usb_chunk_out_count++;

        /* Check if frame is completely assembled */
        bool bFrameComplete = false;
        if (s_out_assembly_len >= 12) /* Minimum gs_host_frame header */
        {
            struct gs_host_frame *pFrame = (struct gs_host_frame *)s_out_assembly_buf;
            uint32_t min_len = (pFrame->flags & GS_CAN_FLAG_FD) ? (12 + 64) : (12 + 8);

            if (s_out_assembly_len >= min_len || rx_len < EP3_MAX_PKT_SIZE)
            {
                bFrameComplete = true;
            }
        }

        if (bFrameComplete)
        {
            enqueue_out_frame((const struct gs_host_frame *)s_out_assembly_buf);
            s_out_assembly_len = 0;
            g_stats.usb_out_count++;
        }
    }
    else
    {
        /* Overflow or invalid packet: reset assembly buffer */
        s_out_assembly_len = 0;
    }

    /* Trigger to receive next OUT packet */
    USBD_SET_PAYLOAD_LEN(EP3, EP3_MAX_PKT_SIZE);
}

/* Background Processing Loop: Dispatches TX & IN Queues */
void usbd_gs_can_process(void)
{
    /* 1. Forward frames from USB OUT queue to CAN Hardware Transmitter */
    struct gs_host_frame tx_frame;
    while (usbd_gs_can_dequeue_out_frame(&tx_frame))
    {
        if (!m253_canfd_transmit(&tx_frame))
        {
            /* If CAN FIFO is momentarily full, re-enqueue or drop */
            break;
        }
    }

    /* 2. Dispatch frames from CAN RX & Echo queue to USB IN (EP2) */
    if (!s_bEp2Busy)
    {
        struct gs_host_frame in_frame;
        if (dequeue_in_frame(&in_frame))
        {
            uint32_t total_len;
            if (in_frame.flags & GS_CAN_FLAG_FD)
            {
                total_len = 12 + 64 + 4; /* 80 bytes */
            }
            else
            {
                total_len = 12 + 8 + 4;  /* 24 bytes */
            }

            memcpy(s_in_active_buf, &in_frame, total_len);
            s_in_total_len = total_len;
            s_in_sent_len = 0;
            s_bEp2Busy = true;

            /* Send first chunk (Packet 1: max 64 bytes) */
            uint32_t chunk1_len = total_len;
            if (chunk1_len > EP2_MAX_PKT_SIZE) chunk1_len = EP2_MAX_PKT_SIZE;

            USBD_MemCopy((uint8_t *)(USBD_BUF_BASE + USBD_GET_EP_BUF_ADDR(EP2)),
                         s_in_active_buf,
                         chunk1_len);

            s_in_sent_len = chunk1_len;
            USBD_SET_PAYLOAD_LEN(EP2, chunk1_len);
            g_stats.usb_chunk_in_count++;
        }
    }
}
