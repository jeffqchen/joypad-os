#ifndef TINK_INPUT_H
#define TINK_INPUT_H

#include <stdint.h>

/*
 * Initialize the controller input layer.
 */
void tink_input_init(void);

/*
 * Update the current controller button state.
 */
void tink_input_process(uint32_t buttons);

/*
 * Process button events, long presses, and repeat timing.
 */
void tink_input_task(void);

/*
 * Notify the input layer that the controller disconnected.
 */
void tink_input_controller_disconnected(void);

#endif /* TINK_INPUT_H */
