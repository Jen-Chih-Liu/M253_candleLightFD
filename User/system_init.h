#ifndef __SYSTEM_INIT_H__
#define __SYSTEM_INIT_H__

#include <stdint.h>
#include "NuMicro.h"
#include "board_config.h"
#include "debug_log.h"

void system_init(void);
void hirc_trim_task(void);

#endif /* __SYSTEM_INIT_H__ */
