#ifndef TINK_COMMAND_H
#define TINK_COMMAND_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Result of starting or processing a repeat.
 *
 * ACCEPTED: the repeat was started, handled, or queued.
 * BUSY: the transport is active but cannot accept the request yet.
 * ENDED: the request cannot be started or the repeat session is invalid.
 */
typedef enum {
    TINK_REPEAT_ACCEPTED,
    TINK_REPEAT_BUSY,
    TINK_REPEAT_ENDED
} tink_repeat_result_t;

/*
 * Logical RetroTINK commands.
 *
 * These commands are independent of the transport.
 * The command layer translates them into IR or serial commands.
 */
typedef enum {
    TINK_CMD_UP,
    TINK_CMD_DOWN,
    TINK_CMD_LEFT,
    TINK_CMD_RIGHT,

    TINK_CMD_OK,
    TINK_CMD_BACK,
    TINK_CMD_MENU,
    TINK_CMD_DIAG,
    TINK_CMD_PROF,

    TINK_CMD_POWER,
    TINK_CMD_SAFE,

    TINK_CMD_INPUT,
    TINK_CMD_OUTPUT,
    TINK_CMD_ADC,
    TINK_CMD_SCALER,
    TINK_CMD_SFX,

    TINK_CMD_COUNT
} tink_command_t;

/*
 * Initialize the command routing layer.
 */
void tink_command_init(void);

/*
 * Send a one-shot logical command.
 *
 * Automatically selects CDC or IR.
 */
bool tink_command_send(tink_command_t command);

/*
 * Start a repeating logical command.
 *
 * Returns ACCEPTED when started, BUSY when it should be retried later,
 * or ENDED when the request cannot be started.
 *
 * IR uses hardware repeat.
 * CDC uses individual queued commands, scheduled by the input layer.
 */
tink_repeat_result_t tink_command_start_repeat(tink_command_t command);

/*
 * Stop the currently repeating command.
 */
void tink_command_stop_repeat(void);

/*
 * Process one repeat tick.
 *
 * ACCEPTED means the tick was handled or queued.
 * BUSY means the active transport cannot accept it yet; retry later.
 * ENDED means the repeat session is no longer valid.
 */
tink_repeat_result_t tink_command_repeat_tick(void);

/*
 * Check whether the CDC transport is connected.
 */
bool tink_command_is_cdc_connected(void);

#endif /* TINK_COMMAND_H */
