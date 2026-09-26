#ifndef __PAW3395_H__
#define __PAW3395_H__

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Transport callbacks, implemented by the port layer (paw3395_port.c).
 * `reg` is the 7-bit sensor register address; the port owns the read/write bit
 * convention and the chip-select timing. */
typedef uint8_t (*paw3395_read_fptr_t)(uint8_t reg, uint8_t *data, uint16_t len);
typedef void (*paw3395_write_fptr_t)(uint8_t reg, const uint8_t *data, uint16_t len);
typedef void (*paw3395_delay_us_fptr_t)(uint32_t period);

enum paw3395_mode {
    PAW3395_MODE_HIGH_PERFORMANCE = 0,
    PAW3395_MODE_LOW_POWER,
    PAW3395_MODE_OFFICE,
    PAW3395_MODE_CORDED_GAMING,
    PAW3395_MODE_COUNT,
};

enum paw3395_lift_cut {
    PAW3395_LIFT_CUT_1MM = 0,
    PAW3395_LIFT_CUT_2MM,
};

struct paw3395_dev {
    paw3395_read_fptr_t     read;
    paw3395_write_fptr_t    write;
    paw3395_delay_us_fptr_t delay_us;

    enum paw3395_mode     mode;
    enum paw3395_lift_cut lift_cut;
    uint16_t cpi;
    bool     initialized;
};

bool paw3395_init(struct paw3395_dev *dev);
void paw3395_set_cpi(struct paw3395_dev *dev, uint16_t cpi);
void paw3395_set_mode(struct paw3395_dev *dev, enum paw3395_mode mode);
void paw3395_set_lift_cut(struct paw3395_dev *dev, enum paw3395_lift_cut cut);
void paw3395_burst(struct paw3395_dev *dev, uint8_t *buf);
void paw3395_shutdown(struct paw3395_dev *dev);

#ifdef __cplusplus
}
#endif

#endif /* __PAW3395_H__ */
