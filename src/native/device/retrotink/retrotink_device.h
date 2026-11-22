#ifndef RETROTINK_DEVICE_H
#define RETROTINK_DEVICE_H

#include <stdint.h>

#include "core/input_event.h"
#include "core/output_interface.h"

extern const OutputInterface retrotink_output_interface;

/*
 * Initialize the RetroTINK output interface.
 *
 * Called by the application after USB Host and router initialization.
 */
void retrotink_device_start(void);

/*
 * Periodic output processing.
 *
 * Called through OutputInterface.task().
 */
void retrotink_device_task(void);

/*
 * Notify the device that its input controller disconnected.
 */
void retrotink_device_controller_disconnected(void);

/*
 * Optional application callback for input events.
 *
 * Allows the application to handle Bluetooth-specific activity
 * tracking and controller feedback without coupling those features
 * to the RetroTINK output module.
 */
typedef void (*retrotink_input_callback_t)(
    const input_event_t *event
);

void retrotink_device_set_input_callback(
    retrotink_input_callback_t callback
);

#endif /* RETROTINK_DEVICE_H */