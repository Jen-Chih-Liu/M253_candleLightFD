#ifndef __DESCRIPTORS_H__
#define __DESCRIPTORS_H__

#include <stdint.h>
#include <stddef.h>
#include "NuMicro.h"
#include "usbd.h"

#define USBD_WCID_VENDOR_CODE      0x20

/* Endpoint Buffer Allocation (Total 1024 Bytes SRAM in M253 USBD) */
#define SETUP_BUF_BASE             0x000UL
#define SETUP_BUF_LEN              8UL

#define EP0_BUF_BASE               (SETUP_BUF_BASE + SETUP_BUF_LEN)
#define EP0_MAX_PKT_SIZE           64UL

#define EP1_BUF_BASE               (EP0_BUF_BASE + EP0_MAX_PKT_SIZE)
#define EP1_MAX_PKT_SIZE           64UL

#define EP2_BUF_BASE               (EP1_BUF_BASE + EP1_MAX_PKT_SIZE)
#define EP2_MAX_PKT_SIZE           64UL

#define EP3_BUF_BASE               (EP2_BUF_BASE + EP2_MAX_PKT_SIZE)
#define EP3_MAX_PKT_SIZE           64UL

extern const uint8_t gu8DeviceDescriptor[];
extern const uint8_t gu8ConfigDescriptor[];
extern const uint8_t gu8StringLang[4];
extern const uint8_t gu8VendorString[];
extern const uint8_t gu8ProductString[];
extern uint8_t gu8StringSerial[];
extern const uint8_t gu8WcidStringDescriptor[18];
extern const uint8_t gu8WcidCompatibleIdDesc[40];
extern const uint8_t gu8WcidExtPropertyDesc[142];

extern const S_USBD_INFO_T gsInfo;

#endif /* __DESCRIPTORS_H__ */
