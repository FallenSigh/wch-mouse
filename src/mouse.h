#ifndef __MOUSE_H__
#define __MOUSE_H__

#include <stdint.h>

#define HID_BTN_LEFT      0x01
#define HID_BTN_RIGHT     0x02
#define HID_BTN_MID       0x04
#define HID_BTN_BACK      0x08
#define HID_BTN_FWD       0x10

#define HID_BTN_MASK      (HID_BTN_LEFT | HID_BTN_RIGHT | HID_BTN_MID \
                           | HID_BTN_BACK | HID_BTN_FWD)

#define HID_MOUSE_EP      DEF_UEP4

#ifdef __cplusplus
extern "C" {
#endif

typedef struct __attribute__((packed)) {
    uint8_t buttons;
    int16_t  dx;
    int16_t  dy;
    int8_t  wheel;
} MouseReport_t;

void mouse_init();
void mouse_scan(uint32_t now_ms);
void mouse_set_cpi(uint16_t cpi);

#ifdef __cplusplus
}
#endif

#endif