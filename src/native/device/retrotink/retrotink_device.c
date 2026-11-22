#include "retrotink_device.h"

#include <stdbool.h>
#include <stdio.h>

#include "core/output_interface.h"
#include "core/router/router.h"

#include "tink_input.h"
#include "ir/ir_tx.h"
#include "usb/tink_cdc.h"

static retrotink_input_callback_t input_callback = NULL;
static bool cdc_mounted_state;

static void retrotink_router_tap(
    output_target_t output,
    uint8_t player_index,
    const input_event_t *event)
{
    (void)output;
    (void)player_index;

    if (input_callback)
        input_callback(event);
}

void retrotink_device_set_input_callback(
    retrotink_input_callback_t callback)
{
    input_callback = callback;
}

void retrotink_device_set_input_state(uint32_t controls)
{
    tink_input_set_state(controls);
}

void retrotink_device_start(void)
{
    ir_tx_init();
    tink_input_init();
    tink_cdc_init();

    cdc_mounted_state = tink_cdc_is_connected();

    router_set_tap(
        OUTPUT_TARGET_RETROTINK,
        retrotink_router_tap
    );
}

void retrotink_device_task(void)
{
    /*
     * A physical CDC mount/unmount changes the selected transport.
     * Reset all logical input state before processing another input
     * tick. Heartbeat online/offline changes are intentionally ignored.
     */
    bool mounted = tink_cdc_is_connected();

    if (mounted != cdc_mounted_state) {
        cdc_mounted_state = mounted;
        tink_input_reset();

        printf("[retrotink_device] CDC %s; input state reset\n",
               mounted ? "mounted" : "unmounted");
    }

    tink_input_task();
    ir_tx_task();
    tink_cdc_task();
}

void retrotink_device_controller_disconnected(void)
{
    tink_input_reset();
}

const OutputInterface retrotink_output_interface = {
    .name = "RetroTINK",
    .target = OUTPUT_TARGET_RETROTINK,
    .init = NULL,
    .core1_task = NULL,
    .task = retrotink_device_task,
};
