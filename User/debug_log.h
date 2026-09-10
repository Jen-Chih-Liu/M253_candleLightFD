#ifndef __DEBUG_LOG_H__
#define __DEBUG_LOG_H__

#include <stdio.h>
#include <stdint.h>
#include "NuMicro.h"

/* Log Level Definitions */
#define LOG_LEVEL_NONE     0   /*!< No debug output */
#define LOG_LEVEL_ERROR    1   /*!< Critical errors only (Bus-Off, Overflow, HardFault) */
#define LOG_LEVEL_INFO     2   /*!< Normal state transitions (Boot, USB enum, CAN baudrate, Start/Stop) */
#define LOG_LEVEL_DEBUG    3   /*!< Detailed progress (64B Chunking, Setup packet breakdown) */
#define LOG_LEVEL_DUMP     4   /*!< Full packet dump (Low-speed diagnosis) */

/* Active Log Level */
#ifndef CURRENT_LOG_LEVEL
#define CURRENT_LOG_LEVEL  LOG_LEVEL_INFO
#endif

#if (CURRENT_LOG_LEVEL >= LOG_LEVEL_ERROR)
#define LOG_ERROR(...)     printf(__VA_ARGS__)
#else
#define LOG_ERROR(...)     do {} while(0)
#endif

#if (CURRENT_LOG_LEVEL >= LOG_LEVEL_INFO)
#define LOG_INFO(...)      printf(__VA_ARGS__)
#else
#define LOG_INFO(...)      do {} while(0)
#endif

#if (CURRENT_LOG_LEVEL >= LOG_LEVEL_DEBUG)
#define LOG_DEBUG(...)     printf(__VA_ARGS__)
#else
#define LOG_DEBUG(...)     do {} while(0)
#endif

#if (CURRENT_LOG_LEVEL >= LOG_LEVEL_DUMP)
#define LOG_DUMP(...)      printf(__VA_ARGS__)
#else
#define LOG_DUMP(...)      do {} while(0)
#endif

/* Non-blocking traffic statistics */
typedef struct {
    volatile uint32_t can_rx_count;
    volatile uint32_t can_tx_count;
    volatile uint32_t can_tx_echo_count;
    volatile uint32_t can_bus_err_count;
    volatile uint32_t usb_in_count;
    volatile uint32_t usb_out_count;
    volatile uint32_t usb_chunk_in_count;
    volatile uint32_t usb_chunk_out_count;
} traffic_stats_t;

extern traffic_stats_t g_stats;

#endif /* __DEBUG_LOG_H__ */
