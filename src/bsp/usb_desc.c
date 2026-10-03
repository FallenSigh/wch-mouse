/********************************** (C) COPYRIGHT *******************************
 * File Name          : usb_desc.c
 * Author             : wch-mouse
 * Version            : V1.0.0
 * Date               : 2026/09/15
 * Description        : Composite USB device descriptor: CDC (debug) + HID mouse.
 *                      Derived from WCH SimulateCDC (CDC) + CompositeKM (mouse HID).
 *********************************************************************************
 * Copyright (c) 2024 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

#include "usb_desc.h"

/* Device Descriptor */
const uint8_t MyDevDescr[] = {
    0x12,                                                   // bLength
    0x01,                                                   // bDescriptorType (Device)
    0x00, 0x02,                                             // bcdUSB 2.00
    0x00,                                                   // bDeviceClass (use IAD)
    0x00,                                                   // bDeviceSubClass
    0x00,                                                   // bDeviceProtocol
    DEF_USBD_UEP0_SIZE,                                     // bMaxPacketSize0
    (uint8_t)DEF_USB_VID, (uint8_t)(DEF_USB_VID >> 8),      // idVendor
    (uint8_t)DEF_USB_PID, (uint8_t)(DEF_USB_PID >> 8),      // idProduct
    DEF_IC_PRG_VER, 0x00,                                   // bcdDevice
    0x01,                                                   // iManufacturer
    0x02,                                                   // iProduct
    0x03,                                                   // iSerialNumber
    0x01,                                                   // bNumConfigurations
};

/* ===========================================================================
 *  Configuration Descriptor -- Full Speed
 *
 *  Layout (3 interfaces):
 *    IAD[0,1]   CDC Comm + CDC Data
 *    Interface 0 CDC Comm   EP3 IN (interrupt)
 *    Interface 1 CDC Data   EP2 OUT, EP2 IN (bulk)
 *    Interface 2 HID Mouse  EP4 IN (interrupt)
 * =========================================================================== */
const uint8_t MyCfgDescr_FS[] = {
    /* Configuration Descriptor */
    0x09, 0x02, 0x64, 0x00, 0x03, 0x01, 0x00, 0x80, 0x32,

    /* IAD Descriptor (interfaces 0/1 = CDC) */
    0x08, 0x0B, 0x00, 0x02, 0x02, 0x02, 0x01, 0x00,

    /* Interface 0 (CDC Comm) descriptor */
    0x09, 0x04, 0x00, 0x00, 0x01, 0x02, 0x02, 0x01, 0x00,

    /* CDC Functional Descriptors (Header / Call Mgmt / Acm / Union) */
    0x05, 0x24, 0x00, 0x10, 0x01,
    0x05, 0x24, 0x01, 0x00, 0x01,
    0x04, 0x24, 0x02, 0x02,
    0x05, 0x24, 0x06, 0x00, 0x01,

    /* CDC Comm IN endpoint (EP3 interrupt) */
    0x07, 0x05, 0x83, 0x03,
    (uint8_t)DEF_USB_EP3_FS_SIZE, (uint8_t)(DEF_USB_EP3_FS_SIZE >> 8),
    0x01,                                                    // bInterval: 1 ms (FS = 1 kHz)

    /* Interface 1 (CDC Data) descriptor */
    0x09, 0x04, 0x01, 0x00, 0x02, 0x0a, 0x00, 0x00, 0x00,

    /* CDC Data OUT (EP2 bulk) */
    0x07, 0x05, 0x02, 0x02,
    (uint8_t)DEF_USB_EP2_FS_SIZE, (uint8_t)(DEF_USB_EP2_FS_SIZE >> 8),
    0x00,

    /* CDC Data IN (EP2 bulk) */
    0x07, 0x05, 0x82, 0x02,
    (uint8_t)DEF_USB_EP2_FS_SIZE, (uint8_t)(DEF_USB_EP2_FS_SIZE >> 8),
    0x00,

    /* Interface 2 (HID Mouse) descriptor */
    0x09, 0x04, 0x02, 0x00, 0x01, 0x03, 0x01, 0x02, 0x00,  // subclass=boot, proto=mouse

    /* HID Descriptor (mouse) */
    0x09, 0x21, 0x00, 0x01, 0x00, 0x01, 0x22,
    (uint8_t)DEF_MOUSE_REPORT_DESC_LEN,
    (uint8_t)(DEF_MOUSE_REPORT_DESC_LEN >> 8),

    /* HID Mouse IN endpoint (EP4 interrupt)
     * bInterval = 1: at FS = 1 ms = 1 kHz, at HS = 125 us = 8 kHz */
    0x07, 0x05, 0x84, 0x03,
    (uint8_t)DEF_USB_EP4_FS_SIZE, (uint8_t)(DEF_USB_EP4_FS_SIZE >> 8),
    0x01,
};

/* ===========================================================================
 *  Configuration Descriptor -- High Speed (same layout, larger EP sizes)
 * =========================================================================== */
const uint8_t MyCfgDescr_HS[] = {
    /* Configuration Descriptor */
    0x09, 0x02, 0x64, 0x00, 0x03, 0x01, 0x00, 0x80, 0x32,

    /* IAD Descriptor (interfaces 0/1 = CDC) */
    0x08, 0x0B, 0x00, 0x02, 0x02, 0x02, 0x01, 0x00,

    /* Interface 0 (CDC Comm) descriptor */
    0x09, 0x04, 0x00, 0x00, 0x01, 0x02, 0x02, 0x01, 0x00,

    /* CDC Functional Descriptors */
    0x05, 0x24, 0x00, 0x10, 0x01,
    0x05, 0x24, 0x01, 0x00, 0x01,
    0x04, 0x24, 0x02, 0x02,
    0x05, 0x24, 0x06, 0x00, 0x01,

    /* CDC Comm IN endpoint (EP3 interrupt) */
    0x07, 0x05, 0x83, 0x03,
    (uint8_t)DEF_USB_EP3_HS_SIZE, (uint8_t)(DEF_USB_EP3_HS_SIZE >> 8),
    0x01,                                                    // bInterval: 1 (HS = 125 us = 8 kHz)

    /* Interface 1 (CDC Data) descriptor */
    0x09, 0x04, 0x01, 0x00, 0x02, 0x0a, 0x00, 0x00, 0x00,

    /* CDC Data OUT (EP2 bulk) */
    0x07, 0x05, 0x02, 0x02,
    (uint8_t)DEF_USB_EP2_HS_SIZE, (uint8_t)(DEF_USB_EP2_HS_SIZE >> 8),
    0x00,

    /* CDC Data IN (EP2 bulk) */
    0x07, 0x05, 0x82, 0x02,
    (uint8_t)DEF_USB_EP2_HS_SIZE, (uint8_t)(DEF_USB_EP2_HS_SIZE >> 8),
    0x00,

    /* Interface 2 (HID Mouse) descriptor */
    0x09, 0x04, 0x02, 0x00, 0x01, 0x03, 0x01, 0x02, 0x00,

    /* HID Descriptor (mouse) */
    0x09, 0x21, 0x00, 0x01, 0x00, 0x01, 0x22,
    (uint8_t)DEF_MOUSE_REPORT_DESC_LEN,
    (uint8_t)(DEF_MOUSE_REPORT_DESC_LEN >> 8),

    /* HID Mouse IN endpoint (EP4 interrupt) */
    0x07, 0x05, 0x84, 0x03,
    (uint8_t)DEF_USB_EP4_HS_SIZE, (uint8_t)(DEF_USB_EP4_HS_SIZE >> 8),
    0x01,
};

/* ===========================================================================
 *  HID Mouse Report Descriptor
 *
 *  6-byte report:
 *    byte 0    = buttons (5 bits used: L/R/M/Bk/Fwd, 3 bits padding)
 *    bytes 1-2 = X delta (int16 LE, relative)
 *    bytes 3-4 = Y delta (int16 LE, relative)
 *    byte 5    = wheel   (int8, relative)
 * =========================================================================== */
const uint8_t MyMouseReportDesc[] = {
    0x05, 0x01,         // Usage Page (Generic Desktop)
    0x09, 0x02,         // Usage (Mouse)
    0xA1, 0x01,         // Collection (Application)
    0x09, 0x01,         //   Usage (Pointer)
    0xA1, 0x00,         //   Collection (Physical)

    /* Buttons: 5 buttons, 1 bit each, then 3 bits padding to fill byte 0 */
    0x05, 0x09,         //     Usage Page (Buttons)
    0x19, 0x01,         //     Usage Minimum (Button 1)
    0x29, 0x05,         //     Usage Maximum (Button 5)
    0x15, 0x00,         //     Logical Minimum (0)
    0x25, 0x01,         //     Logical Maximum (1)
    0x75, 0x01,         //     Report Size (1)
    0x95, 0x05,         //     Report Count (5)
    0x81, 0x02,         //     Input (Data, Variable, Absolute)
    0x75, 0x03,         //     Report Size (3) -- padding
    0x95, 0x01,         //     Report Count (1)
    0x81, 0x01,         //     Input (Constant)

    /* X / Y: 16-bit signed relative */
    0x05, 0x01,         //     Usage Page (Generic Desktop)
    0x09, 0x30,         //     Usage (X)
    0x09, 0x31,         //     Usage (Y)
    0x16, 0x00, 0x80,   //     Logical Minimum (-32768)
    0x26, 0xFF, 0x7F,   //     Logical Maximum (32767)
    0x75, 0x10,         //     Report Size (16)
    0x95, 0x02,         //     Report Count (2)
    0x81, 0x06,         //     Input (Data, Variable, Relative)

    /* Wheel: 8-bit signed relative */
    0x09, 0x38,         //     Usage (Wheel)
    0x15, 0x81,         //     Logical Minimum (-127)
    0x25, 0x7F,         //     Logical Maximum (+127)
    0x75, 0x08,         //     Report Size (8)
    0x95, 0x01,         //     Report Count (1)
    0x81, 0x06,         //     Input (Data, Variable, Relative)

    0xC0,               //   End Collection (Physical)
    0xC0,               // End Collection (Application)

    /* Vendor-defined Feature report: the configuration/control channel.
     * Deliberately no Report ID - the descriptor has a single input report and
     * this is the single feature report, and an ID here would force one onto
     * the mouse input too. 63 data bytes. */
    0x06, 0x00, 0xFF,   // Usage Page (Vendor-Defined 0xFF00)
    0x09, 0x01,         // Usage (0x01)
    0xA1, 0x01,         // Collection (Application)
    0x09, 0x01,         //   Usage (0x01)
    0x15, 0x00,         //   Logical Minimum (0)
    0x26, 0xFF, 0x00,   //   Logical Maximum (255)
    0x75, 0x08,         //   Report Size (8)
    0x95, 0x3F,         //   Report Count (63)
    0xB1, 0x02,         //   Feature (Data,Var,Abs)
    0xC0,               // End Collection
};

/* The HID descriptor's wDescriptorLength is a literal in usb_desc.h; keep it
 * honest. */
typedef char proto_report_desc_len_check[(sizeof(MyMouseReportDesc) == DEF_MOUSE_REPORT_DESC_LEN) ? 1 : -1];

/* Language Descriptor */
const uint8_t MyLangDescr[] = { 0x04, 0x03, 0x09, 0x04 };

/* Manufacturer Descriptor */
const uint8_t MyManuInfo[] = {
    0x0E, 0x03, 'w', 0, 'c', 0, 'h', 0, '.', 0, 'c', 0, 'n', 0
};

/* Product Information */
const uint8_t MyProdInfo[] = {
    0x16, 0x03,
    'w', 0x00, 'c', 0x00, 'h', 0x00, '-', 0x00, 'm', 0x00, 'o', 0x00,
    'u', 0x00, 's', 0x00, 'e', 0x00
};

/* Serial Number Information */
const uint8_t MySerNumInfo[] = {
    0x16, 0x03,
    '0', 0x00, '1', 0x00, '2', 0x00, '3', 0x00, '4', 0x00, '5', 0x00,
    '6', 0x00, '7', 0x00, '8', 0x00, '9', 0x00
};

/* Device Qualified Descriptor */
const uint8_t MyQuaDesc[] = {
    0x0A, 0x06, 0x00, 0x02, 0xFF, 0xFF, 0xFF, 0x40, 0x01, 0x00,
};

/* Device BOS Descriptor */
const uint8_t MyBOSDesc[] = {
    0x05, 0x0F, 0x0C, 0x00, 0x01,
    0x07, 0x10, 0x02, 0x02, 0x00, 0x00, 0x00,
};

/* USB Full-Speed Mode, Other speed configuration Descriptor */
uint8_t TAB_USB_FS_OSC_DESC[sizeof(MyCfgDescr_HS)] = {
    0x09, 0x07,
};

/* USB High-Speed Mode, Other speed configuration Descriptor */
uint8_t TAB_USB_HS_OSC_DESC[sizeof(MyCfgDescr_FS)] = {
    0x09, 0x07,
};
