/*
 * BT2TINK-specific TinyUSB host configuration.
 *
 * Keep the shared src/tusb_config.h upstream-clean. This wrapper first loads
 * that shared configuration, then overrides only the options required by
 * BT2TINK. The BT2TINK app include directory must precede the repository's
 * src directory in the target include path.
 */

#ifndef BT2TINK_TUSB_CONFIG_WRAPPER_H_
#define BT2TINK_TUSB_CONFIG_WRAPPER_H_

/*
 * Load the shared Joypad OS configuration located after apps/bt2tink in the
 * include search path. GCC/Clang support #include_next.
 */
#include_next "tusb_config.h"

/* BT2TINK uses an FTDI USB-to-serial adapter for its RetroTINK CDC link. */
#undef CFG_TUH_CDC
#define CFG_TUH_CDC 1

#define CFG_TUH_CDC_FTDI 1

/* BT2TINK does not use USB-host XInput; retain upstream XInput for other apps. */
#undef CFG_TUH_XINPUT
#define CFG_TUH_XINPUT 0

/* Set the FTDI adapter's line coding when it is enumerated. */
#define CFG_TUH_CDC_LINE_CODING_ON_ENUM \
    { 2000000, CDC_LINE_CODING_STOP_BITS_1, \
      CDC_LINE_CODING_PARITY_NONE, 8 }

#endif /* BT2TINK_TUSB_CONFIG_WRAPPER_H_ */
