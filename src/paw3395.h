#ifndef __PAW3395_H__
#define __PAW3395_H__

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool paw3395_init();
void paw3395_set_cpi(uint16_t cpi);
void paw3395_burst(uint8_t* buf);
void paw3395_shutdown();

#ifdef __cplusplus
}
#endif

#endif /* __PAW3395_H__ */
