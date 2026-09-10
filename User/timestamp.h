#ifndef __TIMESTAMP_H__
#define __TIMESTAMP_H__

#include <stdint.h>
#include "NuMicro.h"

void timestamp_init(void);
uint32_t timer_get_us(void);

#endif /* __TIMESTAMP_H__ */
