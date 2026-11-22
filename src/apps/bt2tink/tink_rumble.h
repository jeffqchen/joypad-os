#ifndef TINK_RUMBLE_H
#define TINK_RUMBLE_H

#include <stdint.h>
#include <stdbool.h>

void tink_rumble_init(void);
void tink_rumble_process(uint32_t buttons);
void tink_rumble_task(void);
void tink_rumble_stop_all(void);

#endif