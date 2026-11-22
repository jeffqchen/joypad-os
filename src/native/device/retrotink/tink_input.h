#ifndef TINK_INPUT_H
#define TINK_INPUT_H

#include <stdint.h>

/*
 * Initialize the logical input state machine.
 */
void tink_input_init(void);

/*
 * Replace the complete logical-control state.
 */
void tink_input_set_state(uint32_t controls);

/*
 * Clear all pressed and transient input state and cancel every repeat.
 */
void tink_input_reset(void);

/*
 * Process logical button edges, holds, gestures, and repeat timing.
 */
void tink_input_task(void);

#endif /* TINK_INPUT_H */
