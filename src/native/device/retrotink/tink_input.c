#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "retrotink_config.h"
#include "tink_input.h"
#include "command/tink_command.h"

#include "native/device/retrotink/retrotink_device.h"
#include "pico/time.h"

static uint32_t current_controls;
static uint32_t previous_controls;

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

/* Currently repeating logical control. */
static retrotink_input_t active_repeat_control;
static bool repeat_active;
static uint64_t next_repeat_time;

/* Home long press. */
static uint64_t home_press_time;
static bool home_timer_active;
static bool home_command_sent;

/* L3 + R3 SAFE gesture. */
static uint64_t safe_press_time;
static bool safe_timer_active;
static bool safe_command_attempted;
static bool safe_ir_repeat_active;

static bool control_pressed(uint32_t controls, retrotink_input_t input)
{
    return (controls & RETROTINK_INPUT_BIT(input)) != 0;
}

void tink_input_set_state(uint32_t controls)
{
    current_controls = controls;
}

void tink_input_reset(void)
{
    current_controls = 0;
    previous_controls = 0;

    /*
     * Stop unconditionally: SAFE's native IR repeat is independent of
     * the ordinary repeat-selection state.
     */
    tink_command_stop_repeat();

    repeat_active = false;
    active_repeat_control = RETROTINK_INPUT_COUNT;
    next_repeat_time = 0;

    home_press_time = 0;
    home_timer_active = false;
    home_command_sent = false;

    safe_press_time = 0;
    safe_timer_active = false;
    safe_command_attempted = false;
    safe_ir_repeat_active = false;
}

void tink_input_init(void)
{
    tink_command_init();
    selected_command = 0;
    tink_input_reset();
}

/*
 * Preserve the existing repeat-selection priority:
 * LEFT > RIGHT > DOWN > UP > BACK.
 */
static retrotink_input_t get_repeat_control(uint32_t controls)
{
    if (control_pressed(controls, RETROTINK_INPUT_LEFT))
        return RETROTINK_INPUT_LEFT;
    if (control_pressed(controls, RETROTINK_INPUT_RIGHT))
        return RETROTINK_INPUT_RIGHT;
    if (control_pressed(controls, RETROTINK_INPUT_DOWN))
        return RETROTINK_INPUT_DOWN;
    if (control_pressed(controls, RETROTINK_INPUT_UP))
        return RETROTINK_INPUT_UP;
    if (control_pressed(controls, RETROTINK_INPUT_BACK))
        return RETROTINK_INPUT_BACK;

    return RETROTINK_INPUT_COUNT;
}

static tink_command_t get_repeat_command(retrotink_input_t control)
{
    switch (control) {
        case RETROTINK_INPUT_UP:    return TINK_CMD_UP;
        case RETROTINK_INPUT_DOWN:  return TINK_CMD_DOWN;
        case RETROTINK_INPUT_LEFT:  return TINK_CMD_LEFT;
        case RETROTINK_INPUT_RIGHT: return TINK_CMD_RIGHT;
        case RETROTINK_INPUT_BACK:  return TINK_CMD_BACK;
        default:                   return TINK_CMD_BACK;
    }
}

static uint64_t get_repeat_initial_interval(retrotink_input_t control)
{
    if (control == RETROTINK_INPUT_UP ||
        control == RETROTINK_INPUT_DOWN ||
        control == RETROTINK_INPUT_BACK) {
        return REPEAT_NAV_INITIAL_DELAY_US;
    }

    return REPEAT_INITIAL_DELAY_US;
}

static void stop_selected_repeat(void)
{
    if (repeat_active) {
        tink_command_stop_repeat();
        repeat_active = false;
        active_repeat_control = RETROTINK_INPUT_COUNT;
    }
}

static void process_repeat_controls(uint32_t controls, uint64_t now)
{
    const uint32_t safe_combo =
        RETROTINK_INPUT_BIT(RETROTINK_INPUT_L3) |
        RETROTINK_INPUT_BIT(RETROTINK_INPUT_R3);

    retrotink_input_t desired =
        (controls & safe_combo) == safe_combo
            ? RETROTINK_INPUT_COUNT
            : get_repeat_control(controls);

    if (desired == active_repeat_control && repeat_active) {
        if (now < next_repeat_time)
            return;

        if (tink_command_repeat_tick())
            next_repeat_time = now + REPEAT_INTERVAL_US;

        return;
    }

    stop_selected_repeat();

    if (desired != RETROTINK_INPUT_COUNT) {
        tink_command_t command = get_repeat_command(desired);

        if (tink_command_start_repeat(command)) {
            active_repeat_control = desired;
            repeat_active = true;
            next_repeat_time =
                now + get_repeat_initial_interval(desired);

            printf("[tink_input] Repeat started: control %u\n",
                   (unsigned)desired);
        }
    }
}

/*
 * HOME: send POWER once after a 1.5-second hold.
 */
static void process_home_long_press(uint32_t controls, uint64_t now)
{
    bool pressed = control_pressed(controls, RETROTINK_INPUT_HOME);

    if (!pressed) {
        home_timer_active = false;
        home_command_sent = false;
        return;
    }

    if (!home_timer_active) {
        home_press_time = now;
        home_timer_active = true;
        home_command_sent = false;
        return;
    }

    if (!home_command_sent &&
        now - home_press_time >= PS_HOLD_TIME_US) {
        if (tink_command_send(TINK_CMD_POWER))
            printf("[tink_input] HOME long press: POWER\n");

        home_command_sent = true;
    }
}

/*
 * L3 + R3:
 * - Without mounted CDC: start native NEC SAFE repeat immediately.
 * - With mounted CDC: send SAFE once after a two-second hold.
 *   A failed attempt while CDC is offline is not retried each task tick.
 */
static void process_safe_gesture(uint32_t controls, uint64_t now)
{
    const uint32_t combo =
        RETROTINK_INPUT_BIT(RETROTINK_INPUT_L3) |
        RETROTINK_INPUT_BIT(RETROTINK_INPUT_R3);

    bool both_pressed = (controls & combo) == combo;

    if (!both_pressed) {
        if (safe_ir_repeat_active) {
            tink_command_stop_repeat();
            safe_ir_repeat_active = false;
        }

        safe_timer_active = false;
        safe_command_attempted = false;
        return;
    }

    if (!safe_timer_active) {
        safe_press_time = now;
        safe_timer_active = true;
        safe_command_attempted = false;
        stop_selected_repeat();
    }

    if (!tink_command_is_cdc_connected()) {
        if (!safe_ir_repeat_active &&
            tink_command_start_repeat(TINK_CMD_SAFE)) {
            safe_ir_repeat_active = true;
            printf("[tink_input] IR SAFE repeat started\n");
        }
        return;
    }

    if (!safe_command_attempted &&
        now - safe_press_time >= LONG_PRESS_TIME_US) {
        if (tink_command_send(TINK_CMD_SAFE))
            printf("[tink_input] CDC SAFE command sent\n");

        safe_command_attempted = true;
    }
}

static void process_single_presses(uint32_t pressed)
{
    if (control_pressed(pressed, RETROTINK_INPUT_OK))
        tink_command_send(TINK_CMD_OK);

    if (control_pressed(pressed, RETROTINK_INPUT_MENU))
        tink_command_send(TINK_CMD_MENU);

    if (control_pressed(pressed, RETROTINK_INPUT_DIAG))
        tink_command_send(TINK_CMD_DIAG);

    if (control_pressed(pressed, RETROTINK_INPUT_PROF))
        tink_command_send(TINK_CMD_PROF);

    if (control_pressed(pressed, RETROTINK_INPUT_NEXT)) {
        selected_command++;
        if (selected_command >= CYCLE_COMMAND_COUNT)
            selected_command = 0;

        tink_command_send(cycle_commands[selected_command]);
    }

    if (control_pressed(pressed, RETROTINK_INPUT_PREVIOUS)) {
        if (selected_command == 0)
            selected_command = CYCLE_COMMAND_COUNT - 1;
        else
            selected_command--;

        tink_command_send(cycle_commands[selected_command]);
    }
}

void tink_input_task(void)
{
    uint64_t now = time_us_64();
    uint32_t controls = current_controls;
    uint32_t pressed = controls & ~previous_controls;

    process_single_presses(pressed);
    process_repeat_controls(controls, now);
    process_home_long_press(controls, now);
    process_safe_gesture(controls, now);

    previous_controls = controls;
}
