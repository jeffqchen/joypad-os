# BT2TINK application target and build configuration.
#
# This file is added from the CYW43-capable board section in src/CMakeLists.txt.
# Shared source lists and Joypad OS target helpers are inherited from the parent.

option(JEFF_CMSIS_DAP_DEBUG
    "Enable BT2TINK hardware UART debug output on UART1 (GPIO 4/5)"
    OFF
)

add_executable(joypad_bt2tink)
# Keep the ELF and UF2 in the top-level build directory, matching the
# repository's generic Makefile packaging convention. The Pico SDK uses
# ARCHIVE_OUTPUT_DIRECTORY (not RUNTIME_OUTPUT_DIRECTORY) to choose the UF2
# output directory in pico_add_uf2_output().
set_target_properties(joypad_bt2tink PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
)
target_compile_definitions(joypad_bt2tink PRIVATE
    CONFIG_bt2tink=1
    # CONFIG_bt2tink enables the TinyUSB host CDC + FTDI configuration in
    # src/tusb_config.h and disables the XInput host class for this app.
    USE_BOOTSEL_BUTTON=1
    CYW43_LWIP=0
    # BTstack uses async_context, not embedded run loop.
    BTSTACK_USE_CYW43=1
    ENABLE_BTSTACK=1
    # bt_transport.c gates bthid_registry_init() on this (see #188). Manual-BT
    # apps do not go through joypad_add_btstack(), so define it here or the
    # registry is never initialised and every BT controller falls to the
    # generic gamepad driver (--gc-sections discards the whole registry).
    CONFIG_BT_HOST=1
)

# Hardware UART logging is deliberately opt-in. Release builds keep UART stdio
# disabled and do not claim GPIO 4/5 for the debug console.
if(JEFF_CMSIS_DAP_DEBUG)
    message(STATUS "joypad_bt2tink: hardware UART debug output ENABLED (UART1, GPIO 4/5)")
    target_compile_definitions(joypad_bt2tink PRIVATE
        JEFF_CMSIS_DAP_DEBUG=1
        PICO_DEFAULT_UART=1
        PICO_DEFAULT_UART_TX_PIN=4
        PICO_DEFAULT_UART_RX_PIN=5
    )
endif()

# DualSense voice/show modes (opt-in, all disabled by default).
# CONFIG_DS5_DROP_SCREAM doubles as the base voice-engine flag (0x36 audio
# streaming, hci_dump CID tap, buffers); modes layer on top of it:
#   DS5_DROP_SCREAM=1  make bt2tink_pico_w   -> drop-scream + fireworks
#   DS5_FISHER_PRICE=1 make bt2tink_pico_w   -> kids numbers/letters mode
#   DS5_ROBOT=1        make bt2tink_pico_w   -> robot mode
#   DS5_COMPANION=1    make bt2tink_pico_w   -> LLM voice companion bridge
# No BTstack modifications required. Assets: tools/ds5-scream/encode.py.
if(DEFINED ENV{DS5_DROP_SCREAM})
    message(STATUS "joypad_bt2tink: DS5 drop-scream + fireworks ENABLED")
    target_compile_definitions(joypad_bt2tink PRIVATE CONFIG_DS5_DROP_SCREAM=1)
endif()
if(DEFINED ENV{DS5_FISHER_PRICE})
    message(STATUS "joypad_bt2tink: DS5 Fisher-Price mode ENABLED")
    target_compile_definitions(joypad_bt2tink PRIVATE
        CONFIG_DS5_DROP_SCREAM=1
        CONFIG_DS5_FISHER_PRICE=1
    )
endif()
if(DEFINED ENV{DS5_ROBOT})
    message(STATUS "joypad_bt2tink: DS5 Robot mode ENABLED")
    target_compile_definitions(joypad_bt2tink PRIVATE
        CONFIG_DS5_DROP_SCREAM=1
        CONFIG_DS5_ROBOT=1
    )
endif()
if(DEFINED ENV{DS5_COMPANION})
    message(STATUS "joypad_bt2tink: DS5 AI companion mode ENABLED")
    target_compile_definitions(joypad_bt2tink PRIVATE
        CONFIG_DS5_DROP_SCREAM=1
        CONFIG_DS5_COMPANION=1
    )
endif()

# BT2TINK does not use the XInput host source.
set(BT2TINK_USB_HOST_SOURCES ${USB_HOST_SOURCES})
list(REMOVE_ITEM BT2TINK_USB_HOST_SOURCES
    ${PROJECT_SOURCE_DIR}/usb/usbh/xinput/xinput.c
)

target_sources(joypad_bt2tink PUBLIC
    ${CORE_SOURCES}
    ${BT2TINK_USB_HOST_SOURCES}
    ${BTHID_DEVICE_SOURCES}
    ${PROJECT_SOURCE_DIR}/bt/btstack/btstack_host.c
    ${PROJECT_SOURCE_DIR}/bt/btstack/bt_device_db.c
    ${PROJECT_SOURCE_DIR}/bt/transport/bt_transport.c
    ${PROJECT_SOURCE_DIR}/bt/transport/bt_transport_cyw43.c
    ${PROJECT_SOURCE_DIR}/apps/bt2tink/app.c
    ${PROJECT_SOURCE_DIR}/apps/bt2tink/tink_rumble.c
    ${PROJECT_SOURCE_DIR}/native/device/retrotink/retrotink_device.c
    ${PROJECT_SOURCE_DIR}/native/device/retrotink/tink_input.c
    ${PROJECT_SOURCE_DIR}/native/device/retrotink/ir/ir_tx.c
    ${PROJECT_SOURCE_DIR}/native/device/retrotink/usb/tink_cdc.c
    ${PROJECT_SOURCE_DIR}/native/device/retrotink/command/tink_command.c
)

target_include_directories(joypad_bt2tink PUBLIC
    ${PROJECT_SOURCE_DIR}/apps/bt2tink
    ${PROJECT_SOURCE_DIR}/native/device/retrotink
    ${PROJECT_SOURCE_DIR}/usb/usbd
    ${PROJECT_SOURCE_DIR}/bt
    ${PROJECT_SOURCE_DIR}/bt/btstack
    ${PROJECT_SOURCE_DIR}/bt/bthid
    ${BTSTACK_INCLUDES}
)

target_link_libraries(joypad_bt2tink PRIVATE
    pico_stdlib
    pico_multicore
    pico_rand
    pico_cyw43_arch_poll
    pico_btstack_ble
    pico_btstack_classic
    pico_btstack_cyw43
    tinyusb_host
    tinyusb_board
    hardware_pwm
)

joypad_target_common(joypad_bt2tink)
pico_enable_stdio_usb(joypad_bt2tink 0)
if(JEFF_CMSIS_DAP_DEBUG)
    pico_enable_stdio_uart(joypad_bt2tink 1)
else()
    pico_enable_stdio_uart(joypad_bt2tink 0)
endif()
