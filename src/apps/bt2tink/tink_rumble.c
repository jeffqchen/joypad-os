#include "tink_rumble.h"

#include "pico/time.h"
#include "core/buttons.h"
#include "core/services/players/feedback.h"
#include "core/services/players/manager.h"

#include <string.h>

#define RUMBLE_INITIAL_DELAY_MS  500
#define RUMBLE_INTERVAL_MS       600
#define RUMBLE_DURATION_MS       100
#define RUMBLE_STRENGTH          127

typedef struct {
    int player_index;

    uint32_t buttons;
    bool active;
    bool pulse_on;

    uint64_t next_pulse_time;
    uint64_t stop_time;
} tink_rumble_state_t;

static tink_rumble_state_t rumble_state;

void tink_rumble_init(void)
{
    memset(&rumble_state, 0, sizeof(rumble_state));
    rumble_state.player_index = -1;
}


static void rumble_set(uint32_t buttons, bool enabled)
{
    if (rumble_state.player_index < 0)
        return;

    uint8_t left_motor = 0;
    uint8_t right_motor = 0;

    if (enabled) {
        if (buttons & JP_BUTTON_DL) {
            left_motor = RUMBLE_STRENGTH;
        } else if (buttons & JP_BUTTON_DR) {
            right_motor = RUMBLE_STRENGTH;
        }
    }

    feedback_set_rumble(
        rumble_state.player_index,
        left_motor,
        right_motor
    );
}

static bool rumble_button_pressed(uint32_t buttons)
{
    return (buttons & (JP_BUTTON_DL | JP_BUTTON_DR)) != 0;
}

static int find_bluetooth_player_index(void)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {

        if (players[i].dev_addr == -1)
            continue;

        if (players[i].transport == INPUT_TRANSPORT_BT_CLASSIC ||
            players[i].transport == INPUT_TRANSPORT_BT_BLE) {
            return i;
        }
    }

    return -1;
}

static bool bluetooth_player_slot_valid(int player_index)
{
    if (player_index < 0 || player_index >= MAX_PLAYERS)
        return false;

    if (players[player_index].dev_addr == -1)
        return false;

    return players[player_index].transport == INPUT_TRANSPORT_BT_CLASSIC ||
           players[player_index].transport == INPUT_TRANSPORT_BT_BLE;
}

void tink_rumble_process(uint32_t buttons)
{
    bool pressed = rumble_button_pressed(buttons);

    int player_index = find_bluetooth_player_index();

    if (!bluetooth_player_slot_valid(player_index))
        return;

    /* Stop rumble when both directions are released. */
    if (!pressed) {
        if (rumble_state.active) {
            rumble_set(buttons, false);

            rumble_state.active = false;
            rumble_state.pulse_on = false;
            rumble_state.buttons = 0;
            rumble_state.stop_time = 0;
            rumble_state.next_pulse_time = 0;
        }

        return;
    }

    /* Handle a direction change during an active cycle. */
    if (rumble_state.active) {
        if (rumble_state.buttons != buttons) {
            rumble_state.buttons = buttons;

            /* Switch motors immediately if the pulse is ON. */
            if (rumble_state.pulse_on) {
                rumble_set(rumble_state.buttons, true);
            }
        }

        return;
    }

    rumble_state.player_index = player_index;
    rumble_state.buttons = buttons;
    rumble_state.active = true;
    rumble_state.pulse_on = false;

    uint64_t now = time_us_64();

    /* Schedule the first pulse after the initial delay. */
    rumble_state.stop_time = 0;

    rumble_state.next_pulse_time =
        now + (RUMBLE_INITIAL_DELAY_MS * 1000ULL);
}

void tink_rumble_task(void)
{
    if (!rumble_state.active)
        return;

    if (!bluetooth_player_slot_valid(rumble_state.player_index)) {
        tink_rumble_stop_all();
        return;
    }

    uint64_t now = time_us_64();

    /* End the current 100 ms pulse. */
    if (rumble_state.pulse_on &&
        now >= rumble_state.stop_time) {

        rumble_set(rumble_state.buttons, false);

        rumble_state.pulse_on = false;
        rumble_state.stop_time = 0;
    }

    /* Start the next pulse after the 600 ms interval. */
    if (now >= rumble_state.next_pulse_time) {

        rumble_set(rumble_state.buttons, true);

        rumble_state.pulse_on = true;

        rumble_state.stop_time =
            now + (RUMBLE_DURATION_MS * 1000ULL);

        rumble_state.next_pulse_time =
            now + (RUMBLE_INTERVAL_MS * 1000ULL);
    }
}

void tink_rumble_stop_all(void)
{
    memset(&rumble_state, 0, sizeof(rumble_state));
    rumble_state.player_index = -1;
}