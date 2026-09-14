#ifndef __USBD_GS_CAN_H__
#define __USBD_GS_CAN_H__

#include <stdint.h>
#include <stdbool.h>
#include "NuMicro.h"
#include "gs_usb.h"
#include "board_config.h"
#include "debug_log.h"

void usbd_gs_can_init(void);
void usbd_gs_can_process(void);

/* Enqueue an incoming CAN frame or TX Echo frame to be sent to Host over USB */
bool usbd_gs_can_enqueue_in_frame(const struct gs_host_frame *frame);

/* Dequeue a frame received from Host to be transmitted by CAN */
bool usbd_gs_can_dequeue_out_frame(struct gs_host_frame *frame);

/* USBD Interrupt Handlers */
void usbd_gs_can_class_request(void);
void usbd_gs_can_vendor_request(void);
void usbd_gs_can_ctrl_out_done(void);
void usbd_gs_can_ep2_in_handler(void);
void usbd_gs_can_ep3_out_handler(void);

typedef struct {
    uint8_t req_type;
    uint8_t req_code;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} usb_trace_t;

extern volatile usb_trace_t g_usb_trace[16];
extern volatile uint8_t g_usb_trace_idx;

#endif /* __USBD_GS_CAN_H__ */
