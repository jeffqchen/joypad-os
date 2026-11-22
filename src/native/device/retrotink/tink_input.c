
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "retrotink_config.h"
#include "tink_input.h"
#include "command/tink_command.h"

#include "core/buttons.h"
#include "pico/time.h"

static uint32_t current_buttons;
static uint32_t previous_buttons;

/* Triangle and Square command selection. */
static const tink_command_t cycle_commands[] = {
    TINK_CMD_INPUT,
    TINK_CMD_OUTPUT,
    TINK_CMD_ADC,
    TINK_CMD_SCALER,
    TINK_CMD_SFX,
    TINK_CMD_PROF
};

#define CYCLE_COMMAND_COUNT \
    (sizeof(cycle_commands) / sizeof(cycle_commands[0]))

static uint8_t selected_command;

/* Currently repeating controller button. */
static uint32_t active_repeat_button;
static uint64_t next_repeat_time;

/* PS button long press. */
static uint64_t ps_press_time;
static bool ps_timer_active;
static bool ps_command_sent;

/* L3 + R3 long press. */
static uint64_t safe_press_time;
static bool safe_timer_active;
static bool safe_command_sent;
static bool safe_ir_repeat_active = false;

void tink_input_process(uint32_t buttons)
{
    current_buttons = buttons;
}


void tink_input_init(void)
{
    tink_command_init();
    
    current_buttons = 0;
    previous_buttons = 0;

    selected_command = 0;

    active_repeat_button = 0;
    next_repeat_time = 0;

    ps_press_time = 0;
    ps_timer_active = false;

    safe_press_time = 0;
    safe_timer_active = false;
    safe_command_sent = false;

    ps_command_sent = false;
}


/*
 * Repeat priority:
 * Up > Down > Left > Right > Circle.
 */
static uint32_t get_repeat_button(uint32_t buttons)
{
    if (buttons & JP_BUTTON_DL)
        return JP_BUTTON_DL;

    if (buttons & JP_BUTTON_DR)
        return JP_BUTTON_DR;

    if (buttons & JP_BUTTON_DD)
        return JP_BUTTON_DD;

    if (buttons & JP_BUTTON_DU)
        return JP_BUTTON_DU;

    if (buttons & JP_BUTTON_B2)
        return JP_BUTTON_B2;

    return 0;
}


static tink_command_t get_repeat_command(uint32_t button)
{
    switch (button) {

        case JP_BUTTON_DU:
            return TINK_CMD_UP;

        case JP_BUTTON_DD:
            return TINK_CMD_DOWN;

        case JP_BUTTON_DL:
            return TINK_CMD_LEFT;

        case JP_BUTTON_DR:
            return TINK_CMD_RIGHT;

        case JP_BUTTON_B2:
            return TINK_CMD_BACK;

        default:
            return TINK_CMD_BACK;
    }
}

static uint64_t get_repeat_initial_interval(uint32_t button)
{
    if (button == JP_BUTTON_DU ||
        button == JP_BUTTON_DD ||
        button == JP_BUTTON_B2) {
        return REPEAT_NAV_INITIAL_DELAY_US;
    }

    return REPEAT_INITIAL_DELAY_US;
}

/*
 * Handles D-pad and Circle repeat behavior.
 */
static void process_repeat_buttons(
    uint32_t buttons,
    uint64_t now)
{
    uint32_t desired_button =
        get_repeat_button(buttons);

    /* No change to the currently repeating button. */
    if (desired_button == active_repeat_button &&
        active_repeat_button != 0) {

        if (now < next_repeat_time)
            return;

        if (tink_command_repeat_tick()) {
            next_repeat_time = now + REPEAT_INTERVAL_US;
        }

        return;
    }

    /* Stop the previous repeat session. */
    if (active_repeat_button != 0) {
        tink_command_stop_repeat();
        active_repeat_button = 0;
    }

    /* Start the newly selected repeat session immediately. */
    if (desired_button != 0) {

        tink_command_t command =
            get_repeat_command(desired_button);

        if (tink_command_start_repeat(command)) {

            active_repeat_button = desired_button;

            next_repeat_time =
                now + get_repeat_initial_interval(desired_button);

            printf("[tink_input] Repeat started: button 0x%08lX\n",
                   (unsigned long)desired_button);
        }
    }
}

/*
 * PS button:
 * Send POWER immediately after being held
 * for at least 1.5 seconds.
 */
static void process_ps_long_press(
    uint32_t buttons,
    uint64_t now)
{
    bool ps_pressed =
        (buttons & JP_BUTTON_A1) != 0;

    if (!ps_pressed) {
        ps_timer_active = false;
        ps_command_sent = false;
        return;
    }

    if (!ps_timer_active) {
        ps_press_time = now;
        ps_timer_active = true;
        ps_command_sent = false;
        return;
    }

    if (!ps_command_sent &&
        now - ps_press_time >= PS_HOLD_TIME_US) {

        if (tink_command_send(TINK_CMD_POWER)) {
            printf("[tink_input] PS long press: POWER\n");
        }

        ps_command_sent = true;
    }
}

/*
 * L3 + R3:
 * IR: Repeat SAFE while both buttons are held.
 * CDC: Send SAFE once after a two-second hold.
 */
static void process_safe_long_press(
    uint32_t buttons,
    uint64_t now)
{
    const uint32_t combo =
        JP_BUTTON_L3 | JP_BUTTON_R3;

    bool both_pressed =
        (buttons & combo) == combo;

    if (!both_pressed) {
        if (safe_ir_repeat_active) {
            tink_command_stop_repeat();
            safe_ir_repeat_active = false;
        }

        safe_timer_active = false;
        safe_command_sent = false;
        return;
    }

    if (!safe_timer_active) {
        safe_press_time = now;
        safe_timer_active = true;
        safe_command_sent = false;

        /* End any previously active button repeat. */
        if (active_repeat_button != 0) {
            tink_command_stop_repeat();
            active_repeat_button = 0;
        }
    }

    /* IR: Start hardware-managed SAFE repeating. */
    if (!tink_command_is_cdc_connected()) {
        if (!safe_ir_repeat_active &&
            tink_command_start_repeat(TINK_CMD_SAFE)) {

            safe_ir_repeat_active = true;
            printf("[tink_input] IR SAFE repeat started\n");
        }

        return;
    }

    /* CDC: Send SAFE once after two seconds. */
    if (!safe_command_sent &&
        now - safe_press_time >= LONG_PRESS_TIME_US) {

        if (tink_command_send(TINK_CMD_SAFE)) {
            printf("[tink_input] CDC SAFE command sent\n");
            safe_command_sent = true;
        }
    }
}

/*
 * Handles one-shot button presses.
 */
static void process_single_press(uint32_t pressed)
{
    /* Cross = OK */
    if (pressed & JP_BUTTON_B1)
        tink_command_send(TINK_CMD_OK);

    /* Options = Menu */
    if (pressed & JP_BUTTON_S2)
        tink_command_send(TINK_CMD_MENU);

    /* Share = Diagnostic */
    if (pressed & JP_BUTTON_S1)
        tink_command_send(TINK_CMD_DIAG);

    /* Touchpad click = Profile */
    if (pressed & JP_BUTTON_A2)
        tink_command_send(TINK_CMD_PROF);

    /* Triangle = next command */
    if (pressed & JP_BUTTON_B4) {

        selected_command++;

        if (selected_command >= CYCLE_COMMAND_COUNT)
            selected_command = 0;

        tink_command_send(cycle_commands[selected_command]);
    }

    /* Square = previous command */
    if (pressed & JP_BUTTON_B3) {

        if (selected_command == 0)
            selected_command = CYCLE_COMMAND_COUNT - 1;
        else
            selected_command--;

        tink_command_send(cycle_commands[selected_command]);
    }
}


/*
 * Call continuously from app_task().
 */
void tink_input_task(void)
{
    uint64_t now = time_us_64();

    uint32_t buttons = current_buttons;

    uint32_t pressed =
        buttons & ~previous_buttons;

    process_single_press(pressed);

    process_repeat_buttons(buttons, now);

    process_ps_long_press(buttons, now);

    process_safe_long_press(buttons, now);

    previous_buttons = buttons;
}


/*
 * Called when the controller disconnects.
 */
void tink_input_controller_disconnected(void)
{
    current_buttons = 0;
    previous_buttons = 0;

    if (active_repeat_button != 0) {
        tink_command_stop_repeat();
        active_repeat_button = 0;
    }

    next_repeat_time = 0;

    ps_timer_active = false;
    ps_press_time = 0;

    safe_timer_active = false;
    safe_press_time = 0;
    safe_command_sent = false;

    ps_command_sent = false;

    printf("[tink_input] Controller disconnected\n");
}
