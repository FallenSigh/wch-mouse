#include "transport.h"
#include "ch585_usbhs_device.h"
#include "bat.h"

#define USB_HID_EP   DEF_UEP4

/* USBHS_DevEnumStatus alone is not enough: the WCH driver only clears it on a
 * bus reset, so unplugging the cable leaves it set and the router keeps
 * sending into a dead link. The charger's PGOOD pin drops as soon as VBUS
 * goes away, so require it too. */
static bool usb_ready(void)
{
    return (USBHS_DevEnumStatus != 0) && bat_power_good();
}

static bool usb_link_up(void)
{
    return usb_ready();
}

static bool usb_send(const MouseReport_t *rpt)
{
    if (!usb_ready()) {
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
