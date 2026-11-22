
#ifndef RETROTINK_CONFIG_H
#define RETROTINK_CONFIG_H

/*
 * RetroTINK Native Device Configuration
 *
 * Compile-time settings for the RetroTINK output device.
 * Values can be overridden by compiler definitions.
 */


/* ============================================================
 * Hardware Configuration
 * ============================================================ */

/* GPIO connected to the IR transmitter. */
#ifndef IR_TX_GPIO
#define IR_TX_GPIO 12
#endif


/* ============================================================
 * Infrared Transmission
 * ============================================================ */

/* NEC carrier frequency. */
#ifndef IR_CARRIER_HZ
#define IR_CARRIER_HZ 38000
#endif

/* NEC protocol timing, in microseconds. */
#ifndef NEC_LEADER_MARK_US
#define NEC_LEADER_MARK_US       9000
#endif

#ifndef NEC_LEADER_SPACE_US
#define NEC_LEADER_SPACE_US      4500
#endif

#ifndef NEC_BIT_MARK_US
#define NEC_BIT_MARK_US          560
#endif

#ifndef NEC_ZERO_SPACE_US
#define NEC_ZERO_SPACE_US        560
#endif

#ifndef NEC_ONE_SPACE_US
#define NEC_ONE_SPACE_US         1690
#endif

#ifndef NEC_FRAME_GAP_US
#define NEC_FRAME_GAP_US         40000
#endif

#ifndef NEC_REPEAT_SPACE_US
#define NEC_REPEAT_SPACE_US      2250
#endif

#ifndef NEC_REPEAT_GAP_US
#define NEC_REPEAT_GAP_US        96190
#endif


/* ============================================================
 * RetroTINK IR Remote
 * ============================================================ */

/* Address used by the supported RetroTINK IR remote protocol. */
#ifndef TINK_IR_REMOTE_ADDR
#define TINK_IR_REMOTE_ADDR      0x49B6
#endif


/* ============================================================
 * Input Behavior
 * ============================================================ */

/* Long-press detection. */
#ifndef LONG_PRESS_TIME_US
#define LONG_PRESS_TIME_US       2000000ULL
#endif

#ifndef PS_HOLD_TIME_US
#define PS_HOLD_TIME_US          1500000ULL
#endif

/* Button repeat timing. */
#ifndef REPEAT_INITIAL_DELAY_US
#define REPEAT_INITIAL_DELAY_US      400000ULL
#endif

#ifndef REPEAT_INTERVAL_US
#define REPEAT_INTERVAL_US            20000ULL
#endif

#ifndef REPEAT_NAV_INITIAL_DELAY_US
#define REPEAT_NAV_INITIAL_DELAY_US   500000ULL
#endif


/* ============================================================
 * USB CDC Device Identification
 * ============================================================ */

/* FTDI USB identifiers shared by supported RetroTINK generations. */
#ifndef RETROTINK_USB_VID
#define RETROTINK_USB_VID        0x0403
#endif

#ifndef RETROTINK_USB_PID
#define RETROTINK_USB_PID        0x6001
#endif


/* ============================================================
 * USB CDC Communication Timing
 * ============================================================ */

#ifndef TINK_HEARTBEAT_INTERVAL_MS
#define TINK_HEARTBEAT_INTERVAL_MS    1000
#endif

#ifndef TINK_QUERY_TIMEOUT_MS
#define TINK_QUERY_TIMEOUT_MS         500
#endif

#ifndef TINK_COMMAND_INTERVAL_MS
#define TINK_COMMAND_INTERVAL_MS      10
#endif


#endif /* RETROTINK_CONFIG_H */
