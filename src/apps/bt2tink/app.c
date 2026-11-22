// app.c - BT2TINK App Entry Point
// Bluetooth to RetroTINK adapter for Pico W
//
// Uses Pico W's built-in CYW43 Bluetooth to receive controllers,
// outputs as USB HID device.

#include "app.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "platform/platform.h"
#include "app_config.h"

#include "core/buttons.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/button/button.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "core/services/leds/leds.h"

#include "bt/transport/bt_transport.h"
#include "bt/btstack/btstack_host.h"

#include "native/device/retrotink/retrotink_device.h"

#include "usb/usbh/usbh.h"

#include "tink_rumble.h"

static bool last_bt_connected = false;

// Bluetooth idle power-saving state
static uint32_t bt_idle_last_activity_ms = 0;
static uint32_t bt_idle_previous_buttons = 0;
static bool bt_idle_buttons_initialized = false;
static bool bt_idle_disconnect_requested = false;

#ifdef BTSTACK_USE_ESP32
#include "driver/gpio.h"
extern const bt_transport_t bt_transport_esp32;
// Status LED GPIO — board-specific defaults
// Feather ESP32-S3: GPIO 13 (red LED, active high). GPIO 21 is NeoPixel power!
// Seeed XIAO ESP32-S3: GPIO 21 (active low)
#ifndef STATUS_LED_GPIO
#ifdef BOARD_FEATHER_ESP32S3
#define STATUS_LED_GPIO 13
#else
#define STATUS_LED_GPIO 21
#endif
#endif
#ifndef STATUS_LED_ACTIVE_LOW
#ifdef BOARD_FEATHER_ESP32S3
#define STATUS_LED_ACTIVE_LOW 0
#else
#define STATUS_LED_ACTIVE_LOW 1
#endif
#endif
#elif defined(BTSTACK_USE_NRF)
extern const bt_transport_t bt_transport_nrf;
// nRF: LED status handled by ws2812_nrf.c (RGB LEDs driven via neopixel API)
#ifdef OLED_I2C_DISPLAY
#include "core/services/display/display.h"
#include "core/input_event.h"
#include "core/buttons.h"
#endif
#else
#include "pico/cyw43_arch.h"
extern const bt_transport_t bt_transport_cyw43;
#endif

// ============================================================================
// LED STATUS
// ============================================================================

static uint32_t led_last_toggle = 0;
static bool led_state = false;

// Update LED based on connection status
// - Blink (0.8s): No device connected (scanning, connecting, or idle)
// - Solid on: Device connected
static void platform_led_set(bool on)
{
#ifdef BTSTACK_USE_ESP32
    gpio_set_level(STATUS_LED_GPIO, (on ^ STATUS_LED_ACTIVE_LOW) ? 1 : 0);
#elif defined(BTSTACK_USE_NRF)
    // No-op: RGB LEDs driven by ws2812_nrf.c via neopixel_task()
    (void)on;
#else
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on ? 1 : 0);
#endif
}

static void led_status_update(void)
{
    uint32_t now = platform_time_ms();

    typedef struct
    {
        uint32_t duration_ms;
        bool state_off;
    } blink_pattern_t;

    const blink_pattern_t scanning_blink_interval_ms_array[4] = {{50, false},
                                                                 {50, true},
                                                                 {50, false},
                                                                 {850, true}};
    static size_t scanning_blink_index = 0;

    // Actively scanning (button press / pairing window): FAST blink so the
    // press visibly took — even while other devices stay connected.
    if (btstack_host_is_scanning())
    {
        if (now - led_last_toggle >= scanning_blink_interval_ms_array[scanning_blink_index].duration_ms)
        {
            led_state = scanning_blink_interval_ms_array[scanning_blink_index].state_off;
            platform_led_set(led_state);
            led_last_toggle = now;
            scanning_blink_index++;
            scanning_blink_index &= 0x3; // wrap to 0-3
        }
        return;
    }

    if (btstack_classic_get_connection_count() > 0)
    {
        // Device connected - solid on
        if (!led_state)
        {
            platform_led_set(true);
            led_state = true;
        }
    }
    else
    {
        // No device connected - blink
        if (now - led_last_toggle >= 400)
        {
            led_state = !led_state;
            platform_led_set(led_state);
            led_last_toggle = now;
        }
    }
}

// ============================================================================
// BUTTON EVENT HANDLER
// ============================================================================

static void on_button_event(button_event_t event)
{
    switch (event)
    {
    case BUTTON_EVENT_CLICK:
        // Start/extend 60-second BT scan for additional devices
        printf("[app:bt2tink] Starting BT scan (60s)...\n");
        btstack_host_start_timed_scan(60000);
        break;

    case BUTTON_EVENT_HOLD:
        // Long press to disconnect all devices and clear all bonds
        printf("[app:bt2tink] Disconnecting all devices and clearing bonds...\n");
        btstack_host_disconnect_all_devices();
        btstack_host_delete_all_bonds();
        break;

    default:
        break;
    }
}

// ============================================================================
// APP INPUT INTERFACES
// ============================================================================

// BT2TINK has no InputInterface - BT transport handles input internally
// via bthid drivers that call router_submit_input()

const InputInterface **app_get_input_interfaces(uint8_t *count)
{
    *count = 0;
    return NULL;
}

// ============================================================================
// APP OUTPUT INTERFACES
// ============================================================================

static const OutputInterface *output_interfaces[] = {
    &retrotink_output_interface,
};

const OutputInterface **app_get_output_interfaces(uint8_t *count)
{
    *count = sizeof(output_interfaces) / sizeof(output_interfaces[0]);
    return output_interfaces;
}

// ============================================================================
// OLED DISPLAY (XIAO Expansion Board - SSD1306 128x64 I2C)
// ============================================================================

#ifdef OLED_I2C_DISPLAY

// Arrow characters for display (1=up, 2=down, 3=left, 4=right)
#define ARROW_UP "\x01"
#define ARROW_DOWN "\x02"
#define ARROW_LEFT "\x03"
#define ARROW_RIGHT "\x04"

typedef struct
{
    uint32_t mask;
    const char *name;
} button_name_t;

static const button_name_t button_names[] = {
    {JP_BUTTON_DU, ARROW_UP},
    {JP_BUTTON_DR, ARROW_RIGHT},
    {JP_BUTTON_DD, ARROW_DOWN},
    {JP_BUTTON_DL, ARROW_LEFT},
    {JP_BUTTON_B1, "B1"},
    {JP_BUTTON_B2, "B2"},
    {JP_BUTTON_B3, "B3"},
    {JP_BUTTON_B4, "B4"},
    {JP_BUTTON_L1, "L1"},
    {JP_BUTTON_R1, "R1"},
    {JP_BUTTON_L2, "L2"},
    {JP_BUTTON_R2, "R2"},
    {JP_BUTTON_S1, "S1"},
    {JP_BUTTON_S2, "S2"},
    {JP_BUTTON_L3, "L3"},
    {JP_BUTTON_R3, "R3"},
    {JP_BUTTON_A1, "A1"},
    {0, NULL}};

static const char *transport_str(input_transport_t t)
{
    switch (t)
    {
    case INPUT_TRANSPORT_BT_CLASSIC:
        return "BT";
    case INPUT_TRANSPORT_BT_BLE:
        return "BLE";
    default:
        return "?";
    }
}

static void oled_init(void)
{
    display_i2c_config_t cfg = {
        .i2c_inst = 0,
        .pin_sda = 0, // Configured by devicetree on nRF
        .pin_scl = 0,
        .addr = 0x3C,
    };
#ifdef BOARD_FEATHER_NRF52840
    display_init_i2c(&cfg); // SH1107 FeatherWing OLED
    printf("[app:bt2tink] OLED display initialized (SH1107 I2C)\n");
#else
    display_init_ssd1306_i2c(&cfg); // SSD1306 XIAO Expansion Board
    printf("[app:bt2tink] OLED display initialized (SSD1306 I2C)\n");
#endif
}

static input_event_t oled_cached_event;
static bool oled_has_event = false;

static void oled_update_display(void)
{
    static uint32_t last_update = 0;
    static uint32_t last_buttons = 0;
    uint32_t now = platform_time_ms();

    // Cache latest router output
    if (playersCount > 0 && players[0].dev_addr >= 0)
    {
        const input_event_t *ev = router_get_output(OUTPUT_TARGET_RETROTINK, 0);
        if (ev)
        {
            oled_cached_event = *ev;
            oled_has_event = true;
        }
    }

    // Feed button presses to marquee (edge detection)
    uint32_t buttons = oled_has_event ? oled_cached_event.buttons : 0;
    uint32_t newly_pressed = ~last_buttons & buttons;
    last_buttons = buttons;
    for (int i = 0; button_names[i].name != NULL; i++)
    {
        if (newly_pressed & button_names[i].mask)
        {
            display_marquee_add(button_names[i].name);
        }
    }

    if (now - last_update < 50)
        return; // 20fps max
    last_update = now;

    display_clear();

    /*
    // Line 1 (large, y=0): USB output mode
    usb_output_mode_t mode = usbd_get_mode();
    display_text_large(0, 0, usbd_get_mode_name(mode));
    */
    display_text_large(0, 0, "RetroTINK");

    // Separator
    display_hline(0, 17, DISPLAY_WIDTH);

    // Lines 2-4: Controller info
    if (playersCount > 0 && players[0].dev_addr >= 0)
    {
        const char *name = get_player_name(0);
        if (name)
        {
            display_text(0, 20, name);
        }

        char info[22];
        snprintf(info, sizeof(info), "%s dev:%d P%d/%d",
                 transport_str(players[0].transport),
                 players[0].dev_addr,
                 players[0].player_number, playersCount);
        display_text(0, 30, info);

        if (oled_has_event)
        {
            char line[22];
            snprintf(line, sizeof(line), "L:%02X,%02X R:%02X,%02X T:%02X,%02X",
                     oled_cached_event.analog[ANALOG_LX], oled_cached_event.analog[ANALOG_LY],
                     oled_cached_event.analog[ANALOG_RX], oled_cached_event.analog[ANALOG_RY],
                     oled_cached_event.analog[ANALOG_L2], oled_cached_event.analog[ANALOG_R2]);
            display_text(0, 40, line);
        }
    }
    else
    {
        display_text(0, 28, "No controller");
    }

    // Bottom (y=52): Button marquee
    display_marquee_tick();
    display_marquee_render(52);

    display_update();
}

#endif // OLED_I2C_DISPLAY

// AI Code //
// static uint32_t previous_buttons = 0;

static void bt_input_callback(const input_event_t *event)
{
    if (event->valid_fields == 0 ||
        (event->valid_fields & INPUT_VALID_BUTTONS))
    {

        if (!bt_idle_buttons_initialized)
        {
            bt_idle_previous_buttons = event->buttons;
            bt_idle_buttons_initialized = true;
            bt_idle_last_activity_ms = platform_time_ms();
        }
        else if (event->buttons != bt_idle_previous_buttons)
        {
            bt_idle_previous_buttons = event->buttons;
            bt_idle_last_activity_ms = platform_time_ms();
        }

        tink_rumble_process(event->buttons);
    }
}
// End AI Code //

// ============================================================================
// APP INITIALIZATION
// ============================================================================

void app_init(void)
{
    printf("[app:bt2tink] Initializing BT2TINK v%s\n", JOYPAD_VERSION);
#ifdef BTSTACK_USE_ESP32
    printf("[app:bt2tink] ESP32-S3 BLE -> USB HID\n");
    // Init status LED GPIO
    gpio_config_t led_cfg = {
        .pin_bit_mask = (1ULL << STATUS_LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&led_cfg);
    gpio_set_level(STATUS_LED_GPIO, STATUS_LED_ACTIVE_LOW ? 1 : 0); // Start OFF
#elif defined(BTSTACK_USE_NRF)
#ifdef BOARD_FEATHER_NRF52840
    printf("[app:bt2tink] Adafruit Feather nRF52840 Express BLE -> USB HID\n");
#elif defined(BOARD_MAKERDIARY_NRF52840)
    printf("[app:bt2tink] Makerdiary nRF52840 MDK USB Dongle BLE -> USB HID\n");
#else
    printf("[app:bt2tink] Seeed XIAO nRF52840 BLE -> USB HID\n");
#endif
    // RGB LEDs initialized by ws2812_nrf.c via leds_init()
#ifdef OLED_I2C_DISPLAY
    oled_init();
#endif
#else
    // printf("[app:bt2tink] Pico W built-in Bluetooth -> USB HID\n");
    printf("[app:bt2tink] Pico W built-in Bluetooth -> RetroTINK IR\n");
#endif

    // AI Code //
    // Initialize USB host stack
    usbh_init();
    // End of AI Code //

    // Initialize button service (uses BOOTSEL button on Pico W)
    button_init();
    button_set_callback(on_button_event);

    // Configure router for BT2TINK
    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_RETROTINK] = 1,
        },
        .merge_all_inputs = true, // Merge all BT inputs to single output
        .transform_flags = TRANSFORM_FLAGS,
    };
    router_init(&router_cfg);

    // Add default route: BLE Central → RetroTINK
    router_add_route(
        INPUT_SOURCE_BLE_CENTRAL,
        OUTPUT_TARGET_RETROTINK,
        0);

    // AI Code //
    retrotink_device_set_input_callback(bt_input_callback);
    retrotink_device_start();
    // End AI Code //

    // Configure player management
    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    // Initialize Rumble
    tink_rumble_init();

    // Initialize Bluetooth transport
    // Must use bt_init() to set global transport pointer and register drivers
    printf("[app:bt2tink] Initializing Bluetooth...\n");
#ifdef BTSTACK_USE_ESP32
    bt_init(&bt_transport_esp32);
#elif defined(BTSTACK_USE_NRF)
    bt_init(&bt_transport_nrf);
#else
    bt_init(&bt_transport_cyw43);
#endif

    printf("[app:bt2tink] Initialization complete\n");
    printf("[app:bt2tink]   Routing: Bluetooth -> USB Device (HID Gamepad)\n");
    printf("[app:bt2tink]   Player slots: %d\n", MAX_PLAYER_SLOTS);
    printf("[app:bt2tink]   Click BOOTSEL for 60s BT scan\n");
    printf("[app:bt2tink]   Hold BOOTSEL to disconnect all + clear bonds\n");
    printf("[app:bt2tink]   Double-click BOOTSEL to switch USB mode\n");
}

// ============================================================================
// APP TASK (Called from main loop)
// ============================================================================

void app_task(void)
{
    // Process button input
    button_task();

    // Process Bluetooth transport
    bt_task();

    // AI Code //
    usbh_task();

    // Detect controller connection and disconnection
    bool bt_connected = (btstack_classic_get_connection_count() > 0);

    if (bt_connected && !last_bt_connected)
    {
        // Start a fresh idle timer for the new connection.
        bt_idle_last_activity_ms = platform_time_ms();
        bt_idle_previous_buttons = 0;
        bt_idle_buttons_initialized = false;
        bt_idle_disconnect_requested = false;
    }

    if (last_bt_connected && !bt_connected)
    {
        retrotink_device_controller_disconnected();
        tink_rumble_stop_all();

        // Clear idle-tracking state after disconnection.
        bt_idle_last_activity_ms = 0;
        bt_idle_previous_buttons = 0;
        bt_idle_buttons_initialized = false;
        bt_idle_disconnect_requested = false;
    }

    last_bt_connected = bt_connected;

    // Disconnect Bluetooth devices after prolonged digital-button inactivity.
    if (bt_connected &&
        !bt_idle_disconnect_requested &&
        (uint32_t)(platform_time_ms() - bt_idle_last_activity_ms) >=
            BT_IDLE_TIMEOUT_MS)
    {

        printf("[app:bt2tink] Bluetooth idle timeout reached. Disconnecting...\n");

        bt_idle_disconnect_requested = true;
        btstack_host_disconnect_all_devices();
    }

    // Process Bluetooth controller feedback
    tink_rumble_task();

    // End AI Code //

    // Update LED status
    leds_set_connected_devices(btstack_classic_get_connection_count());
    led_status_update();

#ifdef OLED_I2C_DISPLAY
    oled_update_display();
    // Pump one pending page of the async display flush per iteration.
    display_task();
#endif
}