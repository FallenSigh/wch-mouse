#ifndef __TRANSPORT_H__
#define __TRANSPORT_H__

#include <stdbool.h>
#include <stdint.h>

#include "mouse.h"

typedef enum {
    TR_OFFLINE = 0,
    TR_USB,
    TR_BLE,
    TR_RF24
} transport_id_t;

typedef struct transport {
    transport_id_t id;
    const char    *name;
    bool (*init)   (void);
    bool (*link_up)(void);
    bool (*send)   (const MouseReport_t *rpt);
    void (*poll)   (uint32_t now_ms);
} transport_t;

void           transport_router_init(void);
void           transport_router_poll(uint32_t now_ms);
bool           transport_router_publish(const MouseReport_t *rpt);
transport_id_t transport_router_active(void);

#endif
