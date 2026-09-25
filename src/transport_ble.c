/*
 * src/transport_ble.c - BLE HID mouse transport (HOGP)
 *
 * Implements the `transport_t` interface for the CH585's peripheral BLE
 * stack. The mouse report layout (6 bytes: u8 buttons, i16 dx, i16 dy,
 * i8 wheel) is byte-identical to the USB backend, so `mouse.c` is unchanged.
 *
 * Phase 1b: builds on the vendor peripheral library and the GATT profile
 * layer forked into ble/Profile/. We only register the standard HOGP
 * services (HID 0x1812 + Battery 0x180F + Device Information 0x180A) and
 * re-use the vendor's `hidDev_*` task to pump notifications.
 *
 * main-loop only. HidDev_Report is NOT safe to call from an ISR.
 */

#include "transport.h"
#include "log.h"
#include "bat.h"

#ifdef WCH_BLE_ENABLE

#include "CONFIG.h"          /* pulls in CH58xBLE_LIB.h, declares MEM_BUF */
#include "HAL.h"             /* CH58x_BLEInit / HAL_Init */
#include "battservice.h"
#include "devinfoservice.h"
#include "hiddev.h"
#include "hidmouseservice.h"

/* ---- module state ------------------------------------------------------- */

static volatile bool s_connected;

/* Advertised name (GAPS name). GAP_DEVICE_NAME_LEN = 21 excludes NUL. */
static const uint8_t s_devName[GAP_DEVICE_NAME_LEN] = "CH585 Mouse";

/* Battery service: the vendor `battMeasure()` synthesises a dummy ADC
 * value (300) but defers the percentage to a calc callback. We set the
 * service's min/max range to a window that always contains 300 so the
 * early-return paths are skipped, then convert the real voltage to
 * percent inside `s_battCalcCB` using `bat_voltage_mv()`.
 *
 * 2700 mV == 0%, 4200 mV == 100% (Li-ion typical). */
#define BATT_VMIN_MV  2700u
#define BATT_VMAX_MV  4200u

static uint8_t s_battCalcCB(uint16_t adcVal)
{
    (void)adcVal;            /* vendor-supplied placeholder; we read mV directly */
    uint16_t mv = bat_voltage_mv();

    if (mv <= BATT_VMIN_MV) { return 0; }
    if (mv >= BATT_VMAX_MV) { return 100; }
    return (uint8_t)(((uint32_t)(mv - BATT_VMIN_MV) * 100u) /
                     (uint32_t)(BATT_VMAX_MV - BATT_VMIN_MV));
}

/* HW setup/teardown are no-ops: the existing `bat_init()` / `bat_poll()`
 * already drive PA3, and the vendor service only needs the calc hook. */
static void s_battSetupCB(void)   { (void)0; }
static void s_battTeardownCB(void){ (void)0; }

/* ---- HID app callbacks -------------------------------------------------- */

/* Called by HidDev when our CCCD is written. We don't auto-report on
 * notify-enable (the mouse layer publishes reports on motion), so just
 * return SUCCESS. */
static uint8_t s_hidRptCB(uint8_t id, uint8_t type, uint16_t uuid,
                          uint8_t oper, uint16_t *pLen, uint8_t *pData)
{
    (void)id; (void)type; (void)uuid; (void)oper;
    (void)pLen; (void)pData;
    return SUCCESS;
}

static void s_hidEvtCB(uint8_t evt) { (void)evt; }

/* GAP role state change forwarded by HidDev. Tracks link_up() and restarts
 * advertising after a disconnect, matching the vendor example. */
static void s_gapStateCB(gapRole_States_t newState, gapRoleEvent_t *pEvent)
{
    switch (newState & GAPROLE_STATE_ADV_MASK) {
    case GAPROLE_CONNECTED:
        if (pEvent->gap.opcode == GAP_LINK_ESTABLISHED_EVENT) {
            s_connected = true;
            LOG_I("BLE", "connected");
        }
        break;

    case GAPROLE_WAITING:
        /* Disconnect or advertising timeout: drop the link flag and try
         * to advertise again. The peripheral role resumes advertising
         * automatically once we set GAPROLE_ADVERT_ENABLED=TRUE. */
        if (pEvent->gap.opcode == GAP_LINK_TERMINATED_EVENT) {
            s_connected = false;
            LOG_I("BLE", "disconnected reason=0x%x",
                  (unsigned)pEvent->linkTerminate.reason);
        }
        {
            uint8_t adv = TRUE;
            GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(adv), &adv);
        }
        break;

    case GAPROLE_STARTED:
    case GAPROLE_ADVERTISING:
    case GAPROLE_CONNECTED_ADV:
    case GAPROLE_ERROR:
    default:
        break;
    }
}

/* HidDev callback table - fed into HidDev_Register. The vendor profile
 * passes `pfnStateChange` to GAPRole_PeripheralStartDevice for us.
 * Note: the vendor HidDev_Register() takes non-const pointers; the
 * library only stashes them, so the const here is cosmetic and we cast
 * at the call site. */
static hidDevCB_t s_hidCBs = {
    s_hidRptCB,
    s_hidEvtCB,
    NULL,            /* passcodeCB - default from GAPBondMgr */
    s_gapStateCB,
};

/* HID idle timeout + feature flags. */
static hidDevCfg_t s_hidCfg = {
    60000,                                       /* idle ms */
    HID_FLAGS_REMOTE_WAKE | HID_FLAGS_NORMALLY_CONNECTABLE,
};

/* ---- transport_t glue --------------------------------------------------- */

static bool ble_link_up(void)
{
    return s_connected;
}

static bool ble_send(const MouseReport_t *rpt)
{
    if (!s_connected) { return false; }

    /* HidDev_Report returns SUCCESS (0) on a queued notification, or a
     * ble* error otherwise. We only need a coarse bool. */
    return HidDev_Report(HID_RPT_ID_MOUSE_IN, HID_REPORT_TYPE_INPUT,
                         sizeof(MouseReport_t), (uint8_t *)rpt) == SUCCESS;
}

static bool ble_init(void)
{
    /* Vendor init order:
     *   1) HidDev_Init - registers GATT services and the HidDev TMOS task.
     *      HidDev schedules START_DEVICE_EVT which (on next TMOS tick)
     *      calls GAPRole_PeripheralStartDevice, advertising us.
     *   2) Hid_AddService - HID GATT table + included Battery service.
     *   3) HidDev_Register - install our state/evt/ring callbacks; sets
     *      the idle timeout and HID feature flags.
     *   4) Batt_Setup - plug our voltage->percent calculator into the
     *      vendor battery service so its measurement hook reads our ADC.
     */
    HidDev_Init();
    if (Hid_AddService() != SUCCESS) {
        LOG_E("BLE", "Hid_AddService failed");
        return false;
    }
    HidDev_Register(&s_hidCfg, &s_hidCBs);

    /* Connect our ADC to the battery service. The vendor range arguments
     * are only consulted when no calcCB is registered, so they don't
     * actually bound the percentage - we just need to keep the dummy
     * adc=300 inside the [min, max] window. */
    Batt_Setup(0, 0, 65535, s_battSetupCB, s_battTeardownCB, s_battCalcCB);

    /* GAP device name (Generic Access Service). */
    GGS_SetParameter(GGS_DEVICE_NAME_ATT, sizeof(s_devName), (void *)s_devName);

    /* Preferred connection interval: 7.5 ms..10 ms (units of 1.25 ms).
     * Slave latency and supervision timeout are passed to
     * GAPRole_PeripheralConnParamUpdateReq by the vendor profile at link
     * setup time; vendor defaults (0, 500) already match the requested
     * 0 / 500. */
    {
        uint16_t minInt = 6;   /* 7.5 ms */
        uint16_t maxInt = 8;   /* 10 ms */
        GAPRole_SetParameter(GAPROLE_MIN_CONN_INTERVAL, sizeof(minInt), &minInt);
        GAPRole_SetParameter(GAPROLE_MAX_CONN_INTERVAL, sizeof(maxInt), &maxInt);
    }

    LOG_I("BLE", "init");
    return true;
}

const transport_t transport_ble = {
    .id      = TR_BLE,
    .name    = "ble",
    .init    = ble_init,
    .link_up = ble_link_up,
    .send    = ble_send,
    .poll    = NULL,
};

#else  /* WCH_BLE_ENABLE */

const transport_t transport_ble = {
    .id      = TR_BLE,
    .name    = "ble",
    .init    = NULL,
    .link_up = NULL,
    .send    = NULL,
    .poll    = NULL,
};

#endif /* WCH_BLE_ENABLE */