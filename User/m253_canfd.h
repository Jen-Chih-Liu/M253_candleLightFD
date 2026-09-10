#ifndef __M253_CANFD_H__
#define __M253_CANFD_H__

#include <stdint.h>
#include <stdbool.h>
#include "NuMicro.h"
#include "gs_usb.h"
#include "board_config.h"
#include "debug_log.h"

/* Bit Timing Constraints to report to Host */
extern const struct gs_device_bt_const g_can_bt_const;
extern const struct gs_device_bt_const_extended g_can_bt_const_ext;

void m253_canfd_init(void);
void m253_canfd_set_bittiming(const struct gs_device_bittiming *timing);
void m253_canfd_set_data_bittiming(const struct gs_device_bittiming *timing);
void m253_canfd_set_mode(uint32_t mode, uint32_t flags);
bool m253_canfd_transmit(const struct gs_host_frame *frame);
bool m253_canfd_is_enabled(void);
uint32_t m253_canfd_dlc_to_bytes(uint8_t dlc);
uint8_t m253_canfd_bytes_to_dlc(uint32_t bytes);
void m253_canfd_get_state(struct gs_device_state *state);

#endif /* __M253_CANFD_H__ */
