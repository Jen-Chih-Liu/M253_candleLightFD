#include <stdio.h>
#include <string.h>
#include "NuMicro.h"
#include "system_init.h"
#include "descriptors.h"
#include "usbd_gs_can.h"
#include "m253_canfd.h"
#include "timestamp.h"
#include "led.h"
#include "debug_log.h"

extern void usbd_gs_can_ctrl_out_done(void);

/* USBD Interrupt Service Routine */
void USBD_IRQHandler(void)
{
    uint32_t u32IntSts = USBD_GET_INT_FLAG();
    uint32_t u32State = USBD_GET_BUS_STATE();

    /* 1. Floating Detect */
    if (u32IntSts & USBD_INTSTS_FLDET)
    {
        USBD_CLR_INT_FLAG(USBD_INTSTS_FLDET);
        if (USBD_IS_ATTACHED())
        {
            USBD_ENABLE_USB();
            LOG_INFO("[USB] Cable Plugged In\r\n");
        }
        else
        {
            USBD_DISABLE_USB();
            LOG_INFO("[USB] Cable Unplugged\r\n");
        }
    }

    /* 2. Bus Event */
    if (u32IntSts & USBD_INTSTS_BUS)
    {
        USBD_CLR_INT_FLAG(USBD_INTSTS_BUS);

        if (u32State & USBD_STATE_USBRST)
        {
            USBD_ENABLE_USB();
            USBD_SwReset();
            LOG_INFO("[USB] Bus Reset\r\n");
        }
        if (u32State & USBD_STATE_SUSPEND)
        {
            USBD_DISABLE_PHY();
            LOG_DEBUG("[USB] Bus Suspend\r\n");
        }
        if (u32State & USBD_STATE_RESUME)
        {
            USBD_ENABLE_USB();
            LOG_DEBUG("[USB] Bus Resume\r\n");
        }
    }

    /* 3. USB Event */
    if (u32IntSts & USBD_INTSTS_USB)
    {
        /* Setup Packet */
        if (u32IntSts & USBD_INTSTS_SETUP)
        {
            USBD_CLR_INT_FLAG(USBD_INTSTS_SETUP);
            USBD_STOP_TRANSACTION(EP0);
            USBD_STOP_TRANSACTION(EP1);
            USBD_ProcessSetupPacket();
        }

        /* EP0: Control IN */
        if (u32IntSts & USBD_INTSTS_EP0)
        {
            USBD_CLR_INT_FLAG(USBD_INTSTS_EP0);
            USBD_CtrlIn();
        }

        /* EP1: Control OUT */
        if (u32IntSts & USBD_INTSTS_EP1)
        {
            USBD_CLR_INT_FLAG(USBD_INTSTS_EP1);
            USBD_CtrlOut();
            usbd_gs_can_ctrl_out_done();
        }

        /* EP2: Bulk IN (0x81) */
        if (u32IntSts & USBD_INTSTS_EP2)
        {
            USBD_CLR_INT_FLAG(USBD_INTSTS_EP2);
            usbd_gs_can_ep2_in_handler();
        }

        /* EP3: Bulk OUT (0x02) */
        if (u32IntSts & USBD_INTSTS_EP3)
        {
            USBD_CLR_INT_FLAG(USBD_INTSTS_EP3);
            usbd_gs_can_ep3_out_handler();
        }
    }
}

int32_t main(void)
{
    /* 1. Initialize system clock (HIRC 48MHz), CAN pins, Debug UART, Timer */
    system_init();

    /* 2. Initialize Status/Activity LED (PB.0) */
    led_init();

    /* 3. Initialize CAN-FD driver */
    m253_canfd_init();

    /* 4. Open USBD Controller */
    USBD_Open(&gsInfo, usbd_gs_can_class_request, NULL);

    /* 5. Attach Vendor Request Handler */
    g_USBD_pfnVendorRequest = usbd_gs_can_vendor_request;

    /* 6. Initialize Endpoint Layout */
    usbd_gs_can_init();

    /* 7. Start USBD and enable interrupt */
    USBD_Start();
    NVIC_EnableIRQ(USBD_IRQn);

    LOG_INFO("[MAIN] Firmware initialization complete. Ready for host.\r\n");

    uint32_t u32LastStatsTime = timer_get_us();

    /* Main Processing Loop */
    while (1)
    {
        /* Dynamic USB SOF clock auto-trim for crystal-less USB */
        hirc_trim_task();

        /* Update non-blocking LED state machine */
        led_update();

        /* Process queues between USB and CAN FD */
        usbd_gs_can_process();

        /* Periodic 1-Second Heartbeat Statistics */
        uint32_t u32Now = timer_get_us();
        if ((u32Now - u32LastStatsTime) >= 1000000UL)
        {
            u32LastStatsTime = u32Now;
            LOG_INFO("[STATS] CAN RX: %u, TX: %u, Echo: %u | USB IN: %u, OUT: %u | Err: %u\r\n",
                     g_stats.can_rx_count, g_stats.can_tx_count, g_stats.can_tx_echo_count,
                     g_stats.usb_in_count, g_stats.usb_out_count, g_stats.can_bus_err_count);
            LOG_INFO("[CAN_DBG] CCCR=0x%08X TEST=0x%08X PSR=0x%08X IR=0x%08X TXBRP=0x%02X TXBTO=0x%02X RXF0S=0x%08X\r\n",
                     CANFD0->CCCR, CANFD0->TEST, CANFD0->PSR, CANFD0->IR, CANFD0->TXBRP, CANFD0->TXBTO, CANFD0->RXF0S);
        }
    }
}
