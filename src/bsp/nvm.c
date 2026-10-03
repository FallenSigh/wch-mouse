/*
 * src/bsp/nvm.c - generic DataFlash record store. See nvm.h for the contract.
 */

#include "nvm.h"

#include <string.h>

#include "CH58x_common.h"
#include "ISP585.h"
#include "log.h"

/* Largest on-flash record this store stages: 1 magic byte + payload rounded up
 * to a 4-byte multiple. Comfortably above settings (24) and the radio flag (4). */
#define NVM_MAX_RECORD 64u

/* The ROM needs a RAM, 4-byte aligned buffer. One shared staging buffer keeps
 * callers from having to align their own arrays; nvm_* is therefore not
 * reentrant, which is fine - every save runs from main-loop context. */
static __attribute__((aligned(4))) uint8_t s_buf[NVM_MAX_RECORD];

/* On-flash size of [magic][payload], rounded up to a 4-byte multiple. */
static uint16_t nvm_round(uint16_t len)
{
    return (uint16_t)((1u + (uint32_t)len + 3u) & ~3u);
}

static bool nvm_check(const nvm_slot_t *slot)
{
    if ((slot == NULL) || (slot->len == 0u)) {
        return false;
    }

    const uint16_t total = nvm_round(slot->len);

    if (total > NVM_MAX_RECORD) {
        LOG_E("NVM", "record too big: off=0x%04X len=%u", (unsigned)slot->offset,
              (unsigned)slot->len);
        return false;
    }

    if (((uint32_t)slot->offset + total) > EEPROM_MAX_SIZE) {
        LOG_E("NVM", "record out of range: off=0x%04X len=%u", (unsigned)slot->offset,
              (unsigned)slot->len);
        return false;
    }

    return true;
}

bool nvm_load(const nvm_slot_t *slot, void *payload)
{
    if ((payload == NULL) || !nvm_check(slot)) {
        return false;
    }

    EEPROM_READ(slot->offset, s_buf, nvm_round(slot->len));

    if (s_buf[0] != slot->magic) {
        return false;
    }

    memcpy(payload, &s_buf[1], slot->len);
    return true;
}

bool nvm_save(const nvm_slot_t *slot, const void *payload)
{
    if ((payload == NULL) || !nvm_check(slot)) {
        return false;
    }

    const uint16_t total = nvm_round(slot->len);

    memset(s_buf, 0, total);
    s_buf[0] = slot->magic;
    memcpy(&s_buf[1], payload, slot->len);

    const uint32_t er = EEPROM_ERASE(slot->offset, EEPROM_BLOCK_SIZE);
    const uint32_t wr = EEPROM_WRITE(slot->offset, s_buf, total);

    if ((er != 0u) || (wr != 0u)) {
        LOG_E("NVM", "save failed: off=0x%04X len=%u er=%u wr=%u", (unsigned)slot->offset,
              (unsigned)total, (unsigned)er, (unsigned)wr);
        return false;
    }

    return true;
}
