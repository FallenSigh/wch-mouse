#include "transport.h"
#include "ch585_usbhs_device.h"

#define USB_HID_EP   DEF_UEP4

static bool usb_link_up(void)
{
    return USBHS_DevEnumStatus != 0;
}

static bool usb_send(const MouseReport_t *rpt)
{
    if (!USBHS_DevEnumStatus) {
        return false;
    }

    return USBHS_Endp_DataUp(USB_HID_EP, (uint8_t *)rpt, sizeof(*rpt),
                             DEF_UEP_CPY_LOAD) == 0;
}

const transport_t transport_usb = {
    .id      = TR_USB,
    .name    = "usb",
    .init    = NULL,
    .link_up = usb_link_up,
    .send    = usb_send,
    .poll    = NULL,
};
