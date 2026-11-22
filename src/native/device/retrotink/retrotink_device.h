#ifndef RETROTINK_DEVICE_H
#define RETROTINK_DEVICE_H

#include <stdint.h>

#include "core/input_event.h"
#include "core/output_interface.h"

/*
 * Logical controls accepted by the RetroTINK output interface.
 * Physical controller mappings belong to the application.
 */
typedef enum {
    RETROTINK_INPUT_UP = 0,
    RETROTINK_INPUT_DOWN,
    RETROTINK_INPUT_LEFT,
    RETROTINK_INPUT_RIGHT,
    RETROTINK_INPUT_OK,
    RETROTINK_INPUT_BACK,
    RETROTINK_INPUT_MENU,
    RETROTINK_INPUT_DIAG,
    RETROTINK_INPUT_PROF,
    RETROTINK_INPUT_NEXT,
    RETROTINK_INPUT_PREVIOUS,
    RETROTINK_INPUT_HOME,
    RETROTINK_INPUT_L3,
    RETROTINK_INPUT_R3,
    RETROTINK_INPUT_COUNT
} retrotink_input_t;

#define RETROTINK_INPUT_BIT(input) (1u << (input))

extern const OutputInterface retrotink_output_interface;

void retrotink_device_start(void);
void retrotink_device_task(void);
void retrotink_device_controller_disconnected(void);

/*
 * Publish the complete logical-control state. The output interface owns
 * edge detection, hold timing, gestures, and repeat behaviour.
 */
void retrotink_device_set_input_state(uint32_t controls);

/*
 * Optional application callback for Bluetooth activity and rumble.
 */
typedef void (*retrotink_input_callback_t)(
    const input_event_t *event
);

void retrotink_device_set_input_callback(
    retrotink_input_callback_t callback
);

#endif /* RETROTINK_DEVICE_H */
