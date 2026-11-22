#include "../retrotink_config.h"
#include "ir_tx.h"
#include "ir_nec.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "pico/time.h"

#define IR_QUEUE_SIZE 8

typedef enum {
    TX_IDLE,

    TX_LEADER_MARK,
    TX_LEADER_SPACE,
    TX_BIT_MARK,
    TX_BIT_SPACE,
    TX_STOP_MARK,
    TX_GAP,
    TX_REPEAT_LEADER_MARK,
    TX_REPEAT_LEADER_SPACE,
    TX_REPEAT_STOP_MARK,
    TX_REPEAT_GAP

} tx_state_t;

typedef struct {
    uint16_t command;
    bool is_repeat;
    bool full_frame_repeat;
    bool initial_frame;
    uint32_t generation;
} tx_queue_entry_t;

static tx_queue_entry_t command_queue[IR_QUEUE_SIZE];

static uint8_t queue_head;
static uint8_t queue_tail;

static volatile tx_state_t tx_state = TX_IDLE;

static uint32_t tx_frame;
static uint8_t tx_bit_index;

static bool tx_frame_is_repeat;
static bool tx_frame_full_repeat;
static uint32_t tx_frame_repeat_generation;

static uint64_t tx_frame_start_time_us;

/* Repeat-session information shared with the alarm callback. */
static volatile bool repeat_active;
static volatile uint32_t repeat_generation;
static volatile bool repeat_started_event;
static volatile bool repeat_start_notified;

static uint slice_num;
static uint16_t carrier_duty;

static void carrier_on(void)
{
    pwm_set_gpio_level(IR_TX_GPIO, carrier_duty);
}

static void carrier_off(void)
{
    pwm_set_gpio_level(IR_TX_GPIO, 0);
}

/*
 * NEC sends four bytes:
 *
 * Address high byte
 * Address low byte
 * Command high byte
 * Command low byte
 *
 * Bits within each byte are sent LSB first.
 */

static uint32_t make_nec_frame(uint16_t command)
{
    uint8_t addr_hi = (uint8_t)(TINK_IR_REMOTE_ADDR >> 8);
    uint8_t addr_lo = (uint8_t)TINK_IR_REMOTE_ADDR;

    uint8_t cmd_hi = (uint8_t)(command >> 8);
    uint8_t cmd_lo = (uint8_t)command;

    return ((uint32_t)addr_hi) |
           ((uint32_t)addr_lo << 8) |
           ((uint32_t)cmd_hi << 16) |
           ((uint32_t)cmd_lo << 24);
}

static bool repeat_session_is_active(void)
{
    return repeat_active &&
           repeat_generation == tx_frame_repeat_generation;
}

/*
 * Starts one NEC repeat frame.
 */
static int64_t start_repeat_frame(void)
{
    carrier_on();

    tx_state = TX_REPEAT_LEADER_MARK;

    return NEC_LEADER_MARK_US;
}

/*
 * Timer callback: advances the NEC waveform.
 * This runs from the Pico alarm interrupt.
 */
static int64_t ir_alarm_callback(
    alarm_id_t id,
    void *user_data)
{
    (void)id;
    (void)user_data;

    switch (tx_state) {

        /* Full NEC frame */

        case TX_LEADER_MARK:
            carrier_off();
            tx_state = TX_LEADER_SPACE;
            return NEC_LEADER_SPACE_US;

        case TX_LEADER_SPACE:
            carrier_on();
            tx_state = TX_BIT_MARK;
            tx_bit_index = 0;
            return NEC_BIT_MARK_US;

        case TX_BIT_MARK: {
            carrier_off();

            uint32_t bit =
                (tx_frame >> tx_bit_index) & 1u;

            tx_state = TX_BIT_SPACE;

            return bit
                ? NEC_ONE_SPACE_US
                : NEC_ZERO_SPACE_US;
        }

        case TX_BIT_SPACE:
            if (++tx_bit_index < 32) {
                carrier_on();
                tx_state = TX_BIT_MARK;
                return NEC_BIT_MARK_US;
            }

            carrier_on();
            tx_state = TX_STOP_MARK;
            return NEC_BIT_MARK_US;

        case TX_STOP_MARK:
            carrier_off();

            tx_state = TX_GAP;
            return NEC_FRAME_GAP_US;

        case TX_GAP:

            if (tx_frame_is_repeat &&
                repeat_session_is_active() &&
                !tx_frame_full_repeat) {

                /* The first NEC repeat frame begins after the initial frame gap. */
                if (!repeat_start_notified) {
                    repeat_started_event = true;
                    repeat_start_notified = true;
                }

                return start_repeat_frame();
            }

            tx_state = TX_IDLE;
            return 0;

        case TX_REPEAT_LEADER_MARK:
            carrier_off();
            tx_state = TX_REPEAT_LEADER_SPACE;
            return NEC_REPEAT_SPACE_US;

        case TX_REPEAT_LEADER_SPACE:
            carrier_on();
            tx_state = TX_REPEAT_STOP_MARK;
            return NEC_BIT_MARK_US;

        case TX_REPEAT_STOP_MARK:
            carrier_off();
            tx_state = TX_REPEAT_GAP;
            return NEC_REPEAT_GAP_US;

        case TX_REPEAT_GAP:

            if (repeat_session_is_active()) {
                return start_repeat_frame();
            }

            tx_state = TX_IDLE;
            return 0;


        default:
            carrier_off();
            tx_state = TX_IDLE;
            return 0;
    }
}

void ir_tx_init(void)
{
    queue_head = 0;
    queue_tail = 0;

    tx_state = TX_IDLE;

    repeat_active = false;
    repeat_generation = 0;
    repeat_started_event = false;
    repeat_start_notified = false;

    gpio_set_function(IR_TX_GPIO, GPIO_FUNC_PWM);

    slice_num = pwm_gpio_to_slice_num(IR_TX_GPIO);

    uint32_t clock_hz = clock_get_hz(clk_sys);

    uint32_t top =
        (clock_hz / IR_CARRIER_HZ) - 1;

    pwm_config config = pwm_get_default_config();

    pwm_config_set_clkdiv(&config, 1.0f);
    pwm_config_set_wrap(&config, (uint16_t)top);

    carrier_duty = (uint16_t)((top + 1) / 3);

    pwm_init(slice_num, &config, false);

    carrier_off();

    pwm_set_enabled(slice_num, true);
}

/*
 * Adds a normal one-shot command to the queue.
 */
bool ir_tx_enqueue(uint16_t command)
{
    uint8_t next =
        (queue_head + 1) % IR_QUEUE_SIZE;

    if (next == queue_tail) {
        return false;
    }

    command_queue[queue_head].command = command;
    command_queue[queue_head].is_repeat = false;
    command_queue[queue_head].full_frame_repeat = false;
    command_queue[queue_head].initial_frame = false;
    command_queue[queue_head].generation = 0;
    
    queue_head = next;

    return true;
}

/*
 * Starts a repeating command.
 *
 * The first transmission is a complete NEC frame.
 * Subsequent transmissions are NEC repeat frames.
 */
bool ir_tx_start_repeat(uint16_t command)
{
    uint8_t next =
        (queue_head + 1) % IR_QUEUE_SIZE;

    if (next == queue_tail) {
        return false;
    }

    uint32_t new_generation = repeat_generation + 1;

    command_queue[queue_head].command = command;
    command_queue[queue_head].is_repeat = true;
    command_queue[queue_head].full_frame_repeat = false;
    command_queue[queue_head].initial_frame = true;
    command_queue[queue_head].generation = new_generation;
    
    queue_head = next;

    repeat_generation = new_generation;
    repeat_active = true;
    repeat_started_event = false;
    repeat_start_notified = false;

    return true;
}

bool ir_tx_queue_full_repeat_frame(uint16_t command)
{
    /* Only queue another frame when the transmitter is ready. */
    if (!repeat_active ||
        tx_state != TX_IDLE ||
        queue_head != queue_tail) {
        return false;
    }

    uint8_t next =
        (queue_head + 1) % IR_QUEUE_SIZE;

    if (next == queue_tail) {
        return false;
    }

    command_queue[queue_head].command = command;
    command_queue[queue_head].is_repeat = true;
    command_queue[queue_head].full_frame_repeat = true;
    command_queue[queue_head].initial_frame = false;
    command_queue[queue_head].generation = repeat_generation;
    
    queue_head = next;

    return true;
}

bool ir_tx_start_full_repeat(uint16_t command)
{
    uint8_t next =
        (queue_head + 1) % IR_QUEUE_SIZE;

    if (next == queue_tail) {
        return false;
    }

    uint32_t new_generation = repeat_generation + 1;

    command_queue[queue_head].command = command;
    command_queue[queue_head].is_repeat = true;
    command_queue[queue_head].full_frame_repeat = true;
    command_queue[queue_head].initial_frame = true;
    command_queue[queue_head].generation = new_generation;
    
    queue_head = next;

    repeat_generation = new_generation;
    repeat_active = true;

    return true;
}

/*
 * Stops future NEC repeat frames.
 *
 * Any frame already being transmitted is allowed
 * to finish.
 */
void ir_tx_stop_repeat(void)
{
    repeat_active = false;
    repeat_generation++;
}

/* Consume the task-context notification that a repeat frame actually began. */
bool ir_tx_take_repeat_started_event(void)
{
    if (!repeat_started_event) {
        return false;
    }

    repeat_started_event = false;
    return true;
}

/*
 * Starts queued transmissions when the transmitter
 * is idle.
 */
void ir_tx_task(void)
{
    if (tx_state != TX_IDLE) {
        return;
    }

    tx_queue_entry_t entry;

    /*
     * Skip repeat commands that have already
     * been cancelled before transmission begins.
     */
    while (queue_tail != queue_head) {

        entry = command_queue[queue_tail];

        queue_tail = (queue_tail + 1) % IR_QUEUE_SIZE;

        if (entry.is_repeat &&
            (!repeat_active ||
             entry.generation != repeat_generation)) {

            continue;
        }

        goto start_transmission;
    }

    return;

start_transmission:

    tx_frame = make_nec_frame(entry.command);
    tx_bit_index = 0;
    tx_frame_is_repeat = entry.is_repeat;
    tx_frame_repeat_generation = entry.generation;
    tx_frame_full_repeat = entry.full_frame_repeat;

    /* Full-frame repeats begin only after the initial hold delay and are queued. */
    if (entry.is_repeat && !entry.initial_frame && !repeat_start_notified) {
        repeat_started_event = true;
        repeat_start_notified = true;
    }

    tx_frame_start_time_us = time_us_64();
    
    carrier_on();

    tx_state = TX_LEADER_MARK;

    add_alarm_in_us(
        NEC_LEADER_MARK_US,
        ir_alarm_callback,
        NULL,
        true
    );
}