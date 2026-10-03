#ifndef __NVM_H__
#define __NVM_H__

#include <stdbool.h>
#include <stdint.h>

/* Generic DataFlash record store.
 *
 * One record lives in one erased 4 KiB DataFlash block, laid out on flash as
 * [magic][payload ...], zero-padded to a 4-byte multiple. The module owns the
 * ROM EEPROM_* quirks so a caller only describes its payload: the commands
 * address DataFlash by OFFSET (0 = 0x00070000), not the absolute address the
 * memory map shows; the buffer must be RAM, 4-byte aligned; and an erase is a
 * whole 4 KiB block. Small misaligned stack arrays make ERASE/WRITE return
 * non-zero and silently change nothing, so staging happens here.
 *
 * Consumers today: src/app/settings.c (offset 0x1000, magic 0xC7) and
 * src/app/radio_mode.c (offset 0x0000, magic 0x5A). Offsets must not collide
 * with the vendor's 0x6000 / 0x7000 blocks. nvm_* is not reentrant. */
typedef struct {
    uint16_t offset; /* DataFlash offset, block-aligned, one record per block */
    uint8_t magic;   /* written at byte 0; an erased (0xFF) block fails the match */
    uint16_t len;    /* payload size in bytes (excludes the magic byte) */
} nvm_slot_t;

/* Copy the stored payload into `payload`. Returns false when the block does not
 * carry this slot's magic (blank, foreign or older) - the caller then keeps its
 * defaults. */
bool nvm_load(const nvm_slot_t *slot, void *payload);

/* Set the magic, zero-pad, erase the block and write the record. Returns false
 * if the slot is invalid or the ROM reported a failure (logged inside). */
bool nvm_save(const nvm_slot_t *slot, const void *payload);

#endif /* __NVM_H__ */
