/*
 * src/transport/transport_ble.c - BLE HID mouse transport (HOGP)
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

#include <stddef.h>

#include "radio_mode.h"
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

/* Motion accumulator. The BLE link carries roughly one notification per
 * connection interval (~100/s) while mouse_scan publishes at 1 kHz, so most
 * sends are rejected. Reports carry *relative* motion, so a rejected report
 * must NOT be dropped - it is merged here and drained by ble_poll() at
 * whatever rate the link actually allows. Without this the cursor moves at a
 * fraction of the real speed. */
static MouseReport_t s_pending;
static bool          s_have_pending;

static uint32_t s_tx_attempts;
static uint32_t s_tx_sent;
static uint32_t s_tx_stats_ms;
static uint32_t s_tx_ms;

/* Pace notifications to roughly one per connection interval. The main loop
 * has no delay, so polling HidDev_Report unconditionally hammers the stack's
 * buffer allocator hundreds of thousands of times a second and wrecks its
 * timing. One attempt per period is all the link can carry anyway (asked for
 * 7.5-10 ms; the host decides the final value). */
#define BLE_TX_PERIOD_MS  8u

static int16_t clamp_i16(int32_t v)
{
    if (v > 32767)  { return 32767; }
    if (v < -32768) { return -32768; }
    return (int16_t)v;
}

static int8_t clamp_i8(int32_t v)
{
    if (v > 127)  { return 127; }
    if (v < -128) { return -128; }
    return (int8_t)v;
}

/* ---- connection parameter renegotiation ---------------------------------
 *
 * The central decides the connection interval when it connects, and hosts
 * routinely pick something slow for an unknown device (observed: ~4
 * notifications/second, i.e. a ~250 ms interval). The peripheral has to ASK
 * for HID-friendly parameters with GAPRole_PeripheralConnParamUpdateReq -
 * without that request the link never carries enough reports and the cursor
 * stutters badly. The vendor example asks once, ~8 s after connecting, via a
 * TMOS task; we ask both when the host enables our report notifications
 * (by then service discovery is finished, which is when the request is
 * honoured most reliably) and on a delayed fallback timer.
 */
#define BLE_CONN_INTERVAL_MIN   6u     /* units of 1.25 ms -> 7.5 ms */
#define BLE_CONN_INTERVAL_MAX   8u     /* units of 1.25 ms -> 10 ms  */
#define BLE_CONN_LATENCY        0u     /* do not skip connection events */
#define BLE_CONN_TIMEOUT        500u   /* units of 10 ms -> 5 s */
#define BLE_PARAM_UPDATE_TICKS  3200u  /* 2 s, at 625 us per TMOS tick */
#define BLE_EVT_PARAM_UPDATE    0x0001

static tmosTaskID s_bleTaskId;
static uint16_t   s_connHandle;
static bool       s_have_conn;

static void ble_request_fast_conn(void)
{
    if (!s_have_conn) {
        return;
    }

    bStatus_t st = GAPRole_PeripheralConnParamUpdateReq(s_connHandle,
                                                        BLE_CONN_INTERVAL_MIN,
                                                        BLE_CONN_INTERVAL_MAX,
                                                        BLE_CONN_LATENCY,
                                                        BLE_CONN_TIMEOUT,
                                                        (uint8_t)s_bleTaskId);
    LOG_I("BLE", "conn param request 7.5-10ms: 0x%02X", (unsigned)st);
}

static uint16_t ble_task_handler(uint8_t task_id, uint16_t events)
{
    if (events & SYS_EVENT_MSG) {
        uint8_t *pMsg;

        if ((pMsg = tmos_msg_receive(task_id)) != NULL) {
            tmos_msg_deallocate(pMsg);
        }
        return (uint16_t)(events ^ SYS_EVENT_MSG);
    }

    if (events & BLE_EVT_PARAM_UPDATE) {
        ble_request_fast_conn();
        return (uint16_t)(events ^ BLE_EVT_PARAM_UPDATE);
    }

    return 0;
}

/* Advertised name (GAPS name). GAP_DEVICE_NAME_LEN = 21 excludes NUL. */
static const uint8_t s_devName[GAP_DEVICE_NAME_LEN] = "CH585 Mouse";

/* Bonding defaults copied from the vendor HID_Mouse example: Just Works
 * pairing with bonding, no passkey and no MITM. */
#define DEV_PASSCODE       0u
#define DEV_IO_CAP         GAPBOND_IO_CAP_NO_INPUT_NO_OUTPUT
#define DEV_BATT_CRITICAL  6u

#define HID_SERV_UUID_16   0x1812u
#define BATT_SERV_UUID_16  0x180Fu

/* Advertising payload: flags + HID-mouse appearance. Without this the device
 * advertises an empty payload and hosts will not show it. */
static uint8_t s_advData[] = {
    0x02,
    GAP_ADTYPE_FLAGS,
    GAP_ADTYPE_FLAGS_GENERAL | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED,
    0x03,
    GAP_ADTYPE_APPEARANCE,
    LO_UINT16(GAP_APPEARE_HID_MOUSE),
    HI_UINT16(GAP_APPEARE_HID_MOUSE),
};

/* Scan response: complete local name + connection interval range + HID and
 * Battery service UUIDs + TX power. The name here is what a phone shows in
 * its scan list; s_devName above is the GAP service value read after
 * connecting. "CH585 Mouse" is 11 chars, so the AD length byte is 12. */
static uint8_t s_scanRspData[] = {
    0x0C,
    GAP_ADTYPE_LOCAL_NAME_COMPLETE,
    'C', 'H', '5', '8', '5', ' ', 'M', 'o', 'u', 's', 'e',
    0x05,
    GAP_ADTYPE_SLAVE_CONN_INTERVAL_RANGE,
    LO_UINT16(6), HI_UINT16(6),     /* 7.5 ms min */
    LO_UINT16(8), HI_UINT16(8),     /* 10 ms max  */
    0x05,
    GAP_ADTYPE_16BIT_MORE,
    LO_UINT16(HID_SERV_UUID_16), HI_UINT16(HID_SERV_UUID_16),
    LO_UINT16(BATT_SERV_UUID_16), HI_UINT16(BATT_SERV_UUID_16),
    0x02,
    GAP_ADTYPE_POWER_LEVEL,
    0,
};

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
    (void)id; (void)type; (void)uuid;
    (void)pLen; (void)pData;

    /* The host has finished service discovery and subscribed, which is the
     * most reliable moment to ask for HID-friendly connection parameters. */
    if (oper == HID_DEV_OPER_ENABLE) {
        LOG_I("HID", "notify enabled");
        ble_request_fast_conn();
    } else if (oper == HID_DEV_OPER_DISABLE) {
        LOG_I("HID", "notify disabled");
    }

    return SUCCESS;
}

static void s_hidEvtCB(uint8_t evt) { (void)evt; }

/* GAP role state change forwarded by HidDev. Tracks link_up() and restarts
 * advertising after a disconnect, matching the vendor example. */
static void s_gapStateCB(gapRole_States_t newState, gapRoleEvent_t *pEvent)
{
    LOG_I("BLE", "gap state=0x%02X op=0x%02X", (unsigned)newState,
          (unsigned)(pEvent ? pEvent->gap.opcode : 0xFFu));

    switch (newState & GAPROLE_STATE_ADV_MASK) {
    case GAPROLE_CONNECTED:
        if (pEvent->gap.opcode == GAP_LINK_ESTABLISHED_EVENT) {
            gapEstLinkReqEvent_t *est = (gapEstLinkReqEvent_t *)pEvent;

            s_connected  = true;
            s_connHandle = est->connectionHandle;
            s_have_conn  = true;
            LOG_I("BLE", "connected handle=%u", (unsigned)s_connHandle);

            /* Fallback path: ask again once the host has had time to finish
             * service discovery, in case the notify-enable trigger is missed. */
            tmos_start_task(s_bleTaskId, BLE_EVT_PARAM_UPDATE, BLE_PARAM_UPDATE_TICKS);
        }
        break;

    case GAPROLE_WAITING:
        /* Disconnect or advertising timeout: drop the link flag and try
         * to advertise again. The peripheral role resumes advertising
         * automatically once we set GAPROLE_ADVERT_ENABLED=TRUE. */
        if (pEvent->gap.opcode == GAP_LINK_TERMINATED_EVENT) {
            s_connected = false;
            s_have_conn = false;
            s_have_pending = false;      /* drop stale motion from the old link */
            LOG_I("BLE", "disconnected reason=0x%x",
                  (unsigned)pEvent->linkTerminate.reason);
        }
        {
            uint8_t adv = TRUE;
            GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(adv), &adv);
        }
        break;

    case GAPROLE_STARTED: {
        /* Vendor parity: pin the controller to a static address so the host
         * keeps recognising the device (and its bond) across reboots. */
        uint8_t ownAddr[6];

        GAPRole_GetParameter(GAPROLE_BD_ADDR, ownAddr);
        GAP_ConfigDeviceAddr(ADDRTYPE_STATIC, ownAddr);
        break;
    }

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

static bool ble_flush(void)
{
    if (!s_connected || !s_have_pending) {
        return false;
    }

    s_tx_attempts++;

    if (HidDev_Report(HID_RPT_ID_MOUSE_IN, HID_REPORT_TYPE_INPUT,
                      sizeof(s_pending), (uint8_t *)&s_pending) != SUCCESS) {
        return false;               /* link busy - keep accumulating */
    }

    s_pending.buttons = 0;
    s_pending.dx      = 0;
    s_pending.dy      = 0;
    s_pending.wheel   = 0;
    s_have_pending    = false;
    s_tx_sent++;

    return true;
}

static bool ble_send(const MouseReport_t *rpt)
{
    if (!s_connected) { return false; }

    /* Buttons are level-based, motion is relative: accumulate everything so
     * nothing is lost when a notification cannot go out this millisecond. */
    s_pending.buttons = rpt->buttons;
    s_pending.dx      = clamp_i16((int32_t)s_pending.dx + rpt->dx);
    s_pending.dy      = clamp_i16((int32_t)s_pending.dy + rpt->dy);
    s_pending.wheel   = clamp_i8((int32_t)s_pending.wheel + rpt->wheel);
    s_have_pending    = true;

    return true;
}

/* Drain the accumulator. Called from transport_router_poll() in the main
 * loop, i.e. main-loop context, which is the only place HidDev_Report may be
 * called from. */
static void ble_poll(uint32_t now_ms)
{
    if (s_connected && (now_ms - s_tx_ms >= BLE_TX_PERIOD_MS)) {
        s_tx_ms = now_ms;
        ble_flush();
    }

    if (s_connected && (now_ms - s_tx_stats_ms >= 5000u)) {
        s_tx_stats_ms = now_ms;
        LOG_I("BLE", "tx sent %u/%u attempts in 5s", (unsigned)s_tx_sent,
              (unsigned)s_tx_attempts);
        s_tx_sent = 0;
        s_tx_attempts = 0;
    }
}

static bool ble_init(void)
{
    /* One radio, one live transport: when this boot selected 2.4G RF the BLE
     * side stays uninitialised and its link_up() never comes up. */
    if (!radio_mode_is(RADIO_MODE_BLE)) {
        return false;
    }

    s_bleTaskId  = TMOS_ProcessEventRegister(ble_task_handler);
    s_connHandle = 0xFFFFu;
    s_have_conn  = false;

    HidDev_Init();

    /* GAP role parameters MUST be set before the main loop starts pumping
     * TMOS: HidDev_Init queued START_DEVICE_EVT, and that event is what
     * starts the peripheral role and turns advertising on. Setting these
     * afterwards leaves the device radio-silent and undiscoverable. */
    {
        uint8_t advEnable = TRUE;

        GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(advEnable), &advEnable);
        GAPRole_SetParameter(GAPROLE_ADVERT_DATA, sizeof(s_advData), s_advData);
        GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, sizeof(s_scanRspData), s_scanRspData);
    }

    {
        uint16_t minInt = 6;   /* units of 1.25 ms -> 7.5 ms */
        uint16_t maxInt = 8;   /* units of 1.25 ms -> 10 ms  */

        GAPRole_SetParameter(GAPROLE_MIN_CONN_INTERVAL, sizeof(minInt), &minInt);
        GAPRole_SetParameter(GAPROLE_MAX_CONN_INTERVAL, sizeof(maxInt), &maxInt);
    }

    GGS_SetParameter(GGS_DEVICE_NAME_ATT, sizeof(s_devName), (void *)s_devName);

    {
        uint32_t passkey = DEV_PASSCODE;
        /* The vendor example uses WAIT_FOR_REQ, which relies on the host to
         * start pairing. Hosts that do not (many scanners, some PC stacks)
         * leave the link unencrypted, so our ENCRYPT_WRITE CCCD can never be
         * subscribed and HidDev_Report() stays gated. INITIATE makes the
         * peripheral send a slave security request on connect instead. */
        uint8_t  pairMode = GAPBOND_PAIRING_MODE_INITIATE;
        uint8_t  mitm = FALSE;
        uint8_t  ioCap = DEV_IO_CAP;
        uint8_t  bonding = TRUE;

        GAPBondMgr_SetParameter(GAPBOND_PERI_DEFAULT_PASSCODE, sizeof(passkey), &passkey);
        GAPBondMgr_SetParameter(GAPBOND_PERI_PAIRING_MODE, sizeof(pairMode), &pairMode);
        GAPBondMgr_SetParameter(GAPBOND_PERI_MITM_PROTECTION, sizeof(mitm), &mitm);
        GAPBondMgr_SetParameter(GAPBOND_PERI_IO_CAPABILITIES, sizeof(ioCap), &ioCap);
        GAPBondMgr_SetParameter(GAPBOND_PERI_BONDING_ENABLED, sizeof(bonding), &bonding);
    }

    if (Hid_AddService() != SUCCESS) {
        LOG_E("BLE", "Hid_AddService failed");
        return false;
    }
    HidDev_Register(&s_hidCfg, &s_hidCBs);

    {
        uint8_t critical = DEV_BATT_CRITICAL;

        Batt_SetParameter(BATT_PARAM_CRITICAL_LEVEL, sizeof(critical), &critical);
    }

    /* 0..65535 keeps the vendor's placeholder adc=300 inside the window so
     * its bounds checks fall through to our percent calculator. */
    Batt_Setup(0, 0, 65535, s_battSetupCB, s_battTeardownCB, s_battCalcCB);

    LOG_I("BLE", "init");
    return true;
}

const transport_t transport_ble = {
    .id      = TR_BLE,
    .name    = "ble",
    .init    = ble_init,
    .link_up = ble_link_up,
    .send    = ble_send,
    .poll    = ble_poll,
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