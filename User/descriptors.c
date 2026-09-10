#include "descriptors.h"
#include "usbd.h"

/* USB Standard Device Descriptor */
const uint8_t gu8DeviceDescriptor[] =
{
    18,                 /* bLength */
    DESC_DEVICE,        /* bDescriptorType */
    0x00, 0x02,         /* bcdUSB: 2.00 */
    0x00,               /* bDeviceClass: Specified in Interface */
    0x00,               /* bDeviceSubClass */
    0x00,               /* bDeviceProtocol */
    EP0_MAX_PKT_SIZE,   /* bMaxPacketSize0 = 64 */
    0x50, 0x1D,         /* idVendor: 0x1D50 (OpenMoko / candleLight) */
    0x6F, 0x60,         /* idProduct: 0x606F (candleLight) */
    0x00, 0x01,         /* bcdDevice: 1.00 */
    0x01,               /* iManufacturer */
    0x02,               /* iProduct */
    0x03,               /* iSerialNumber */
    0x01                /* bNumConfigurations */
};

/* USB Configuration Descriptor (1 Interface, 2 Bulk Endpoints: EP1 IN, EP2 OUT) */
const uint8_t gu8ConfigDescriptor[] =
{
    /* Configuration Descriptor */
    9,                  /* bLength */
    DESC_CONFIG,        /* bDescriptorType */
    32, 0x00,           /* wTotalLength: 9 + 9 + 7 + 7 = 32 */
    1,                  /* bNumInterfaces: 1 */
    1,                  /* bConfigurationValue: 1 */
    0,                  /* iConfiguration */
    0x80,               /* bmAttributes: Bus powered */
    0xFA,               /* MaxPower: 500 mA (250 * 2mA) */

    /* Interface Descriptor: gs_usb CAN Interface */
    9,                  /* bLength */
    DESC_INTERFACE,     /* bDescriptorType */
    0,                  /* bInterfaceNumber: 0 */
    0,                  /* bAlternateSetting: 0 */
    2,                  /* bNumEndpoints: 2 */
    0xFF,               /* bInterfaceClass: Vendor Specific */
    0xFF,               /* bInterfaceSubClass: Vendor Specific */
    0xFF,               /* bInterfaceProtocol: Vendor Specific */
    0,                  /* iInterface */

    /* Endpoint Descriptor: EP1 IN (Bulk IN, 0x81) */
    7,                  /* bLength */
    DESC_ENDPOINT,      /* bDescriptorType */
    0x81,               /* bEndpointAddress: EP1 IN */
    0x02,               /* bmAttributes: Bulk */
    64, 0x00,           /* wMaxPacketSize: 64 bytes */
    0x00,               /* bInterval: 0 */

    /* Endpoint Descriptor: EP2 OUT (Bulk OUT, 0x02) */
    7,                  /* bLength */
    DESC_ENDPOINT,      /* bDescriptorType */
    0x02,               /* bEndpointAddress: EP2 OUT */
    0x02,               /* bmAttributes: Bulk */
    64, 0x00,           /* wMaxPacketSize: 64 bytes */
    0x00                /* bInterval: 0 */
};

/* String 0: Language ID */
const uint8_t gu8StringLang[4] =
{
    4,
    DESC_STRING,
    0x09, 0x04         /* 0x0409: English (United States) */
};

/* String 1: Manufacturer */
const uint8_t gu8VendorString[] =
{
    24,                 /* Length */
    DESC_STRING,
    'c', 0, 'a', 0, 'n', 0, 'd', 0, 'l', 0, 'e', 0, 'L', 0, 'i', 0, 'g', 0, 'h', 0, 't', 0
};

/* String 2: Product Name */
const uint8_t gu8ProductString[] =
{
    46,                 /* Length */
    DESC_STRING,
    'M', 0, '2', 0, '5', 0, '3', 0, ' ', 0,
    'c', 0, 'a', 0, 'n', 0, 'd', 0, 'l', 0, 'e', 0, 'L', 0, 'i', 0, 'g', 0, 'h', 0, 't', 0, ' ', 0,
    'F', 0, 'D', 0, ' ', 0,
    'U', 0, 'S', 0, 'B', 0
};

/* String 3: Serial Number */
uint8_t gu8StringSerial[] =
{
    26,
    DESC_STRING,
    '0', 0, '0', 0, '0', 0, '0', 0, 'M', 0, '2', 0, '5', 0, '3', 0, 'F', 0, 'D', 0, '0', 0, '1', 0
};

/* Microsoft OS String Descriptor (Index 0xEE) */
const uint8_t gu8WcidStringDescriptor[] =
{
    0x12,               /* Length */
    DESC_STRING,
    'M', 0x00, 'S', 0x00, 'F', 0x00, 'T', 0x00, '1', 0x00, '0', 0x00, '0', 0x00,
    USBD_WCID_VENDOR_CODE,  /* Vendor Code: 0x20 */
    0x00                /* Padding */
};

/* Microsoft Compatible ID Feature Descriptor (Returns "WINUSB\0\0" for Interface 0) */
const uint8_t gu8WcidCompatibleIdDesc[] =
{
    0x28, 0x00, 0x00, 0x00, /* Length: 40 bytes */
    0x00, 0x01,             /* Version 1.0 */
    0x04, 0x00,             /* Index: 0x0004 */
    0x01,                   /* Number of sections: 1 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* Reserved */
    0x00,                   /* Interface number: 0 */
    0x01,                   /* Reserved */
    'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,   /* Compatible ID: "WINUSB\0\0" */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* Sub-compatible ID */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00          /* Reserved */
};

/* Microsoft Extended Properties Feature Descriptor (Interface GUID {c15b4308-04d3-11e6-b3ea-6057189e6443}) */
const uint8_t gu8WcidExtPropertyDesc[] =
{
    0x8E, 0x00, 0x00, 0x00, /* Length: 142 bytes */
    0x00, 0x01,             /* Version 1.0 */
    0x05, 0x00,             /* Index: 0x0005 */
    0x01, 0x00,             /* Number of sections: 1 */
    0x84, 0x00, 0x00, 0x00, /* Section size: 132 bytes */
    0x01, 0x00, 0x00, 0x00, /* Property Data Type: 1 (REG_SZ) */
    0x28, 0x00,             /* Property Name Length: 40 bytes */
    /* "DeviceInterfaceGUID\0" in UTF-16LE */
    'D', 0, 'e', 0, 'v', 0, 'i', 0, 'c', 0, 'e', 0,
    'I', 0, 'n', 0, 't', 0, 'e', 0, 'r', 0, 'f', 0, 'a', 0, 'c', 0, 'e', 0,
    'G', 0, 'U', 0, 'I', 0, 'D', 0, 0, 0,
    0x4E, 0x00, 0x00, 0x00, /* Property Data Length: 78 bytes */
    /* "{c15b4308-04d3-11e6-b3ea-6057189e6443}\0" in UTF-16LE */
    '{', 0, 'c', 0, '1', 0, '5', 0, 'b', 0, '4', 0, '3', 0, '0', 0, '8', 0, '-', 0,
    '0', 0, '4', 0, 'd', 0, '3', 0, '-', 0,
    '1', 0, '1', 0, 'e', 0, '6', 0, '-', 0,
    'b', 0, '3', 0, 'e', 0, 'a', 0, '-', 0,
    '6', 0, '0', 0, '5', 0, '7', 0, '1', 0, '8', 0, '9', 0, 'e', 0, '6', 0, '4', 0, '4', 0, '3', 0,
    '}', 0, 0, 0
};

const uint8_t *gpu8UsbString[] =
{
    gu8StringLang,
    gu8VendorString,
    gu8ProductString,
    gu8StringSerial
};

const S_USBD_INFO_T gsInfo =
{
    (uint8_t *)gu8DeviceDescriptor,
    (uint8_t *)gu8ConfigDescriptor,
    (uint8_t **)gpu8UsbString,
    NULL,
    NULL,
    NULL,
    NULL
};
