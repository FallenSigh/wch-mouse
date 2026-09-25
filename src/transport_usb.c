#include "transport.h"
#include "ch585_usbhs_device.h"
#include "bat.h"
#include "log.h"

#define USB_HID_EP   DEF_UEP4

/* Window after a VBUS (re-)plug during which the PHY is kept up to let a
 * possible host enumerate, even while a radio link owns the mouse. */
#define USB_PROBE_MS 800u

/* USB is usable only when a host has actually enumerated and configured us.
 * Two traps make every naive check wrong:
 *   - USBHS_DevEnumStatus is set only on SET_CONFIGURATION but cleared only
 *     on a bus reset, so unplugging from a host leaves it stuck at 1;
 *   - bat_power_good() only means VBUS is present, which is equally true for
 *     a dumb charger - and a charger never raises a bus reset or sends
 *     SET_CONFIGURATION.
 * So usb_ready() clears the device state itself whenever VBUS goes away, and
 * reports no link whenever the PHY is powered down. */
static bool     s_usb_enumerated;
static bool     s_phy_on;
static uint8_t  s_enum_logged;
static bool     s_vbus_prev;
static bool     s_probing;
static uint32_t s_probe_until_ms;
static uint32_t s_tx_attempts;
static uint32_t s_tx_ok;
static uint32_t s_tx_busy;
static uint32_t s_stat_ms;

/* USBHS_DevConfig / USBHS_DevEnumStatus are only cleared by the driver on a
 * bus reset, so whenever we power the PHY down ourselves the stale
 * "enumerated" state has to be dropped here. */
static void usb_forget(void)
{
    USBHS_DevConfig     = 0;
    USBHS_DevEnumStatus = 0;
    s_usb_enumerated    = false;
}

/* Keep the USBHS PHY and its PLL powered only while it can be used. The PHY
 * costs 10-20 mA, which is pure waste on battery, and USBHS_Device_Init() is
 * a full re-init on its ENABLE path while DISABLE powers the PHY PLL down -
 * so it is safe to toggle as VBUS and the active link come and go. */
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
    } else {
        usb_forget();
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
        usb_forget();
        return false;
    }

    if (!s_phy_on) {
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
    const bool vbus     = bat_power_good();
    const bool usb_owns = (transport_router_active() == TR_USB);
    const bool ble_owns = (transport_router_active() == TR_BLE);

    /* A fresh VBUS (re-)plug while the radio owns the mouse: keep the PHY up
     * briefly so a host attached to that cable can still enumerate and take
     * the link over. */
    if (vbus && !s_vbus_prev && ble_owns) {
        s_probing        = true;
        s_probe_until_ms = now_ms + USB_PROBE_MS;
    }
    s_vbus_prev = vbus;

    if (s_probing && (int32_t)(now_ms - s_probe_until_ms) >= 0) {
        s_probing = false;
    }

    /* PHY on while USB is the active link, while no radio link owns the mouse
     * (so a host can be detected), or during the probe window; otherwise off. */
    usb_phy_set(vbus && (usb_owns || !ble_owns || s_probing));

    if (now_ms - s_stat_ms >= 5000u) {
        s_stat_ms = now_ms;
        LOG_I("USB", "tx att %u ok %u busy %u in 5s", (unsigned)s_tx_attempts,
              (unsigned)s_tx_ok, (unsigned)s_tx_busy);
        s_tx_attempts = 0;
        s_tx_ok = 0;
        s_tx_busy = 0;
    }

    /* Diagnostic: shows whether a re-plugged host re-enumerates at all
     * (enum 0 -> 1) or whether the device never comes back. */
    {
        const uint8_t en = (USBHS_DevEnumStatus != 0) ? 1u : 0u;

        if (en != s_enum_logged) {
            s_enum_logged = en;
            LOG_I("USB", "enum=%u vbus=%u", (unsigned)en,
                  (unsigned)(vbus ? 1u : 0u));
        }
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

    s_tx_attempts++;

    if (USBHS_Endp_DataUp(USB_HID_EP, (uint8_t *)rpt, sizeof(*rpt),
                          DEF_UEP_CPY_LOAD) == 0) {
        s_tx_ok++;
        return true;
    }

    s_tx_busy++;
    return false;
}

const transport_t transport_usb = {
    .id      = TR_USB,
    .name    = "usb",
    .init    = usb_init,
    .link_up = usb_link_up,
    .send    = usb_send,
    .poll    = usb_poll,
};
