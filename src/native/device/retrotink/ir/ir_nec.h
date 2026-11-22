
#ifndef IR_NEC_H
#define IR_NEC_H

#include <stdint.h>

typedef enum {
    TINK_IR_REMOTE_BUTTON_PWR = 0x1AE5,
    TINK_IR_REMOTE_MENU       = 0x5CA3,
    TINK_IR_REMOTE_UP         = 0x18E7,
    TINK_IR_REMOTE_DOWN       = 0x10EF,
    TINK_IR_REMOTE_LEFT       = 0x57A8,
    TINK_IR_REMOTE_RIGHT      = 0x4FB0,
    TINK_IR_REMOTE_OK         = 0x53AC,
    TINK_IR_REMOTE_BACK       = 0x42BD,
    TINK_IR_REMOTE_DIAG       = 0x28D7,
    TINK_IR_REMOTE_STAT       = 0x29D6,
    TINK_IR_REMOTE_INPUT      = 0x11EE,
    TINK_IR_REMOTE_OUTPUT     = 0x20DF,
    TINK_IR_REMOTE_SCALER     = 0x21DE,
    TINK_IR_REMOTE_SFX        = 0x22DD,
    TINK_IR_REMOTE_ADC        = 0x23DC,
    TINK_IR_REMOTE_PROF       = 0x24DB,

    TINK_IR_REMOTE_1          = 0x0BF4,
    TINK_IR_REMOTE_2          = 0x07F8,
    TINK_IR_REMOTE_3          = 0x03FC,
    TINK_IR_REMOTE_4          = 0x0AF5,
    TINK_IR_REMOTE_5          = 0x06F9,
    TINK_IR_REMOTE_6          = 0x02FD,
    TINK_IR_REMOTE_7          = 0x09F6,
    TINK_IR_REMOTE_8          = 0x05FA,
    TINK_IR_REMOTE_9          = 0x01FE,
    TINK_IR_REMOTE_10         = 0x25DA,
    TINK_IR_REMOTE_11         = 0x26D9,
    TINK_IR_REMOTE_12         = 0x27D8,

    TINK_IR_REMOTE_GAIN       = 0x2BD4,
    TINK_IR_REMOTE_PHASE      = 0x2DD2,
    TINK_IR_REMOTE_PAUSE      = 0x56A9,
    TINK_IR_REMOTE_SAFE       = 0x2ED1,
    TINK_IR_REMOTE_GENLOCK    = 0x2CD3,
    TINK_IR_REMOTE_BUFFER     = 0x2FD0,

    TINK_IR_REMOTE_4K         = 0x30CF,
    TINK_IR_REMOTE_1080P      = 0x31CE,
    TINK_IR_REMOTE_1440P      = 0x32CD,
    TINK_IR_REMOTE_480P       = 0x33CC,

    TINK_IR_REMOTE_RES1       = 0x34CB,
    TINK_IR_REMOTE_RES2       = 0x35CA,
    TINK_IR_REMOTE_RES3       = 0x36C9,
    TINK_IR_REMOTE_RES4       = 0x37C8,

    TINK_IR_REMOTE_AUX1       = 0x38C7,
    TINK_IR_REMOTE_AUX2       = 0x39C6,
    TINK_IR_REMOTE_AUX3       = 0x3AC5,
    TINK_IR_REMOTE_AUX4       = 0x3BC4,
    TINK_IR_REMOTE_AUX5       = 0x3CC3,
    TINK_IR_REMOTE_AXU6       = 0x3DC2,
    TINK_IR_REMOTE_AUX7       = 0x3EC1,
    TINK_IR_REMOTE_AUX8       = 0x3FC0

} tink_ir_command_t;

#endif
