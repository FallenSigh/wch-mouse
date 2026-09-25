#include "transport.h"
#include "ch585_usbhs_device.h"
#include "bat.h"
#include "log.h"

#define USB_HID_EP   DEF_UEP4

/* USB is usable only when a host has actually enumerated and configured us.
 * Two traps make every naive check wrong:
 *   - USBHS_DevEnumStatus is set only on SET_CONFIGURATION but cleared only
 *     on a bus reset, so unplugging from a host leaves it stuck at 1;
 *   - bat_power_good() only means VBUS is present, which is equally true for
 *     a dumb charger - and a charger never raises a bus reset or sends
 *     SET_CONFIGURATION.
 * So usb_ready() clears the device state itself whenever VBUS goes away. */
static bool    s_usb_enumerated;
static bool    s_phy_on;
static uint8_t s_enum_logged;

/* Keep the USBHS PHY and its PLL powered only while VBUS is present. The PHY
 * costs 10-20 mA, which is pure waste on battery, and USBHS_Device_Init() is
 * a full re-init on its ENABLE path while DISABLE powers the PHY PLL down -
 * so it is safe to toggle as VBUS comes and goes. The PHY still has to stay
 * on for a guest charger (VBUS but no host): it is the only way to notice a
 * host arriving, while usb_ready() independently refuses to hand the link
 * over unless a host really enumerated us. */
static void usb_phy_set(bool on)
{
    if (on == s_phy_on) {
        return;
    }

    s_phy_on = on;
    USBHS_Device_Init(on ? ENABLE : DISABLE);

    if (on) {
        /* DISABLE powered the USB PLL down, and USBHS_Device_Init(ENABLE)
         * switches it straight back on with no settling time - at boot the
         * rest of the init covers it, on a re-plug nothing does. Give the
         * PLL a moment to lock so the host can actually enumerate us. */
        mDelaymS(5);
    }

    LOG_I("USB", "phy %s", on ? "on" : "off");
}

/* A host is the only thing that can set USBHS_DevEnumStatus (it happens in
 * the SET_CONFIGURATION handler) and the driver only clears it on a bus
 * reset. So when VBUS disappears, forget the whole device state ourselves:
 * that keeps a dumb charger - which can never set it - from looking like a
 * host, and gives a re-plugged host a clean slate to enumerate into. */
static bool usb_ready(void)
{
    if (!bat_power_good()) {
        USBHS_DevConfig     = 0;
        USBHS_DevEnumStatus = 0;
        s_usb_enumerated    = false;
        return false;
    }

    s_usb_enumerated = (USBHS_DevEnumStatus != 0);
    return s_usb_enumerated;
}

static bool usb_init(void)
{
    usb_phy_set(bat_power_good());
    return true;
}

static void usb_poll(uint32_t now_ms)
{
    const uint8_t en = (USBHS_DevEnumStatus != 0) ? 1u : 0u;

    (void)now_ms;
    usb_phy_set(bat_power_good());

    /* Diagnostic: shows whether a re-plugged host re-enumerates at all
     * (enum 0 -> 1) or whether the device never comes back. */
    if (en != s_enum_logged) {
        s_enum_logged = en;
        LOG_I("USB", "enum=%u vbus=%u", (unsigned)en,
              (unsigned)(bat_power_good() ? 1u : 0u));
    }
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
    .init    = usb_init,
    .link_up = usb_link_up,
    .send    = usb_send,
    .poll    = usb_poll,
};
