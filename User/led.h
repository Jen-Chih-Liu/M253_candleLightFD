#ifndef __LED_H__
#define __LED_H__

#include <stdint.h>
#include <stdbool.h>
#include "board_config.h"

void led_init(void);
void led_set_channel_active(bool active);
void led_activity_trigger(void);
void led_set_identify(bool enable);
void led_update(void);

#endif /* __LED_H__ */
