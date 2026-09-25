#include <stddef.h>

#include "transport.h"
#include "log.h"

extern const transport_t transport_usb;
#ifdef WCH_BLE_ENABLE
extern const transport_t transport_ble;
#endif

/* Priority order: s_backends[0] wins. USB is wired and always present, so it
 * outranks the radio backends added later. */
static const transport_t * const s_backends[] = {
    &transport_usb,
#ifdef WCH_BLE_ENABLE
    &transport_ble,
#endif
};

#define BACKEND_COUNT ((uint8_t)(sizeof(s_backends) / sizeof(s_backends[0])))

static const transport_t *s_active;

static void router_reselect(void)
{
    const transport_t *next = NULL;

    for (uint8_t i = 0; i < BACKEND_COUNT; i++) {
        if (s_backends[i]->link_up && s_backends[i]->link_up()) {
            next = s_backends[i];
            break;
        }
    }

    if (next != s_active) {
        s_active = next;
        if (s_active) {
            LOG_I("TRANSPORT", "active: %s", s_active->name);
        } else {
            LOG_D("TRANSPORT", "no link");
        }
    }
}

void transport_router_init(void)
{
    for (uint8_t i = 0; i < BACKEND_COUNT; i++) {
        if (s_backends[i]->init) {
            s_backends[i]->init();
        }
    }

    s_active = NULL;
    router_reselect();
}

void transport_router_poll(uint32_t now_ms)
{
    router_reselect();

    for (uint8_t i = 0; i < BACKEND_COUNT; i++) {
        if (s_backends[i]->poll) {
            s_backends[i]->poll(now_ms);
        }
    }
}

bool transport_router_publish(const MouseReport_t *rpt)
{
    if (s_active && s_active->send) {
        return s_active->send(rpt);
    }

    return false;
}

transport_id_t transport_router_active(void)
{
    return s_active ? s_active->id : TR_OFFLINE;
}
