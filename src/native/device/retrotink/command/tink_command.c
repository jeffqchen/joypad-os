#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "tink_command.h"

#include "../ir/ir_nec.h"
#include "../ir/ir_tx.h"
#include "../usb/tink_cdc.h"

typedef struct {
    uint16_t ir_code;
    const char *serial_command;
} tink_command_mapping_t;

/* Keep transport-specific command definitions together in one table. */
static const tink_command_mapping_t command_map[TINK_CMD_COUNT] = {
    [TINK_CMD_UP]     = { TINK_IR_REMOTE_UP,     "remote up" },
    [TINK_CMD_DOWN]   = { TINK_IR_REMOTE_DOWN,   "remote down" },
    [TINK_CMD_LEFT]   = { TINK_IR_REMOTE_LEFT,   "remote left" },
    [TINK_CMD_RIGHT]  = { TINK_IR_REMOTE_RIGHT,  "remote right" },
    [TINK_CMD_OK]     = { TINK_IR_REMOTE_OK,     "remote ok" },
    [TINK_CMD_BACK]   = { TINK_IR_REMOTE_BACK,   "remote back" },
    [TINK_CMD_MENU]   = { TINK_IR_REMOTE_MENU,   "remote menu" },
    [TINK_CMD_DIAG]   = { TINK_IR_REMOTE_DIAG,   "remote diag" },
    [TINK_CMD_PROF]   = { TINK_IR_REMOTE_PROF,   "remote prof" },
    [TINK_CMD_POWER]  = { TINK_IR_REMOTE_BUTTON_PWR, "remote pwr" },
    [TINK_CMD_SAFE]   = { TINK_IR_REMOTE_SAFE,   "safemode" },
    [TINK_CMD_INPUT]  = { TINK_IR_REMOTE_INPUT,  "remote input" },
    [TINK_CMD_OUTPUT] = { TINK_IR_REMOTE_OUTPUT, "remote output" },
    [TINK_CMD_ADC]    = { TINK_IR_REMOTE_ADC,    "remote adc" },
    [TINK_CMD_SCALER] = { TINK_IR_REMOTE_SCALER, "remote scaler" },
    [TINK_CMD_SFX]    = { TINK_IR_REMOTE_SFX,    "remote sfx" },
};

/* Tracks which transport owns the current repeat session. */
static bool repeat_using_ir;
static bool repeat_using_cdc;
static tink_command_t active_repeat_command;

static const tink_command_mapping_t *get_command_mapping(tink_command_t command)
{
    if ((unsigned)command >= TINK_CMD_COUNT) {
        return NULL;
    }

    return &command_map[command];
}

void tink_command_init(void)
{
    repeat_using_ir = false;
    repeat_using_cdc = false;
    active_repeat_command = TINK_CMD_UP;
}

bool tink_command_send(tink_command_t command)
{
    const tink_command_mapping_t *mapping = get_command_mapping(command);

    if (mapping == NULL) {
        return false;
    }

    /* A mounted CDC transport remains authoritative, even while offline. */
    if (tink_cdc_is_connected()) {
        if (!tink_cdc_is_online()) {
            /* Home/POWER becomes an explicit wakeup while the device is offline. */
            return command == TINK_CMD_POWER && tink_cdc_wakeup();
        }

        return mapping->serial_command != NULL &&
               tink_cdc_send_command(mapping->serial_command);
    }

    return ir_tx_enqueue(mapping->ir_code);
}

static bool command_uses_full_frame_repeat(tink_command_t command)
{
    return command == TINK_CMD_UP ||
           command == TINK_CMD_DOWN ||
           command == TINK_CMD_BACK;
}

bool tink_command_start_repeat(tink_command_t command)
{
    const tink_command_mapping_t *mapping = get_command_mapping(command);

    tink_command_stop_repeat();

    if (mapping == NULL) {
        return false;
    }

    /* CDC repeats are individual serial commands and require an online device. */
    if (tink_cdc_is_connected()) {
        if (!tink_cdc_is_online() || mapping->serial_command == NULL ||
            !tink_cdc_send_command(mapping->serial_command)) {
            return false;
        }

        active_repeat_command = command;
        repeat_using_cdc = true;
        return true;
    }

    bool started;
    if (command_uses_full_frame_repeat(command)) {
        started = ir_tx_start_full_repeat(mapping->ir_code);
    } else {
        started = ir_tx_start_repeat(mapping->ir_code);
    }

    if (!started) {
        return false;
    }

    active_repeat_command = command;
    repeat_using_ir = true;
    return true;
}

bool tink_command_repeat_tick(void)
{
    if (repeat_using_ir) {
        if (command_uses_full_frame_repeat(active_repeat_command)) {
            const tink_command_mapping_t *mapping =
                get_command_mapping(active_repeat_command);

            return mapping != NULL &&
                   ir_tx_queue_full_repeat_frame(mapping->ir_code);
        }

        return true;
    }

    if (repeat_using_cdc) {
        if (!tink_cdc_is_connected() || !tink_cdc_is_online()) {
            tink_command_stop_repeat();
            return false;
        }

        const tink_command_mapping_t *mapping =
            get_command_mapping(active_repeat_command);

        return mapping != NULL && mapping->serial_command != NULL &&
               tink_cdc_send_command(mapping->serial_command);
    }

    return false;
}

void tink_command_stop_repeat(void)
{
    if (repeat_using_ir) {
        ir_tx_stop_repeat();
    }

    repeat_using_ir = false;
    repeat_using_cdc = false;
    active_repeat_command = TINK_CMD_UP;
}

bool tink_command_is_cdc_connected(void)
{
    return tink_cdc_is_connected();
}