#ifndef __BOARD_CONFIG_H__
#define __BOARD_CONFIG_H__

#include "NuMicro.h"

/* CAN-FD Hardware Pin Configuration */
#define BOARD_CAN0_RX_PORT     PC
#define BOARD_CAN0_RX_PIN      BIT4
#define BOARD_CAN0_TX_PORT     PC
#define BOARD_CAN0_TX_PIN      BIT5

/* Debug UART Configuration (UART4 on PA.2/PA.3) */
#ifndef DEBUG_PORT
#define DEBUG_PORT             UART4
#endif
#define DEBUG_UART_BAUDRATE    115200

/* Status and Activity LED Pin Configuration (PB.0) */
#define BOARD_HAS_LED          1
#define LED_PORT               PB
#define LED_PIN                BIT0
#define LED_ACTIVE_LEVEL       1    /*!< 1: High active, 0: Low active */

/* 120 Ohm Termination: Hardware Jumper (No GPIO software control) */
#define BOARD_HAS_SW_TERMINATION 0

/* Buffer Queue Capacities (Total ~3.84 KB in 16KB SRAM) */
#define CAN_TX_RING_SIZE       16    /*!< Frames from Host (USB OUT) -> CAN TX */
#define CAN_RX_RING_SIZE       32    /*!< Frames from CAN RX & Echo -> Host (USB IN) */

/* CAN Clock Frequency (HIRC 48MHz directly feeds CANFD0) */
#define CANFD_CORE_CLOCK       48000000UL

#endif /* __BOARD_CONFIG_H__ */
