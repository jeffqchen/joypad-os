#include "retrotink_device.h"

#include "core/output_interface.h"
#include "core/router/router.h"

#include "tink_input.h"
#include "ir/ir_tx.h"
#include "usb/tink_cdc.h"

/*
 * Optional application callback for input events.
 */
static retrotink_input_callback_t input_callback = NULL;

/*
 * Router tap owned by the RetroTINK output module.
 */
static void retrotink_router_tap(
    output_target_t output,
    uint8_t player_index,
    const input_event_t *event)
{
    (void)output;
    (void)player_index;

    tink_input_process(event->buttons);

    if (input_callback) {
        input_callback(event);
    }
}

void retrotink_device_set_input_callback(
    retrotink_input_callback_t callback)
{
    input_callback = callback;
}

void retrotink_device_start(void)
{
    ir_tx_init();
    tink_input_init();
    tink_cdc_init();

    router_set_tap(
        OUTPUT_TARGET_RETROTINK,
        retrotink_router_tap
    );
}

void retrotink_device_task(void)
{
    tink_input_task();
    ir_tx_task();
    tink_cdc_task();
}

void retrotink_device_controller_disconnected(void)
{
    tink_input_controller_disconnected();
}

/*
 * Joypad OS OutputInterface registration.
 */
const OutputInterface retrotink_output_interface = {
    .name = "RetroTINK",
    .target = OUTPUT_TARGET_RETROTINK,

    /*
     * Initialized explicitly by app_init() after USB Host
     * and router initialization.
     */
    .init = NULL,

    .core1_task = NULL,
    .task = retrotink_device_task,
};
