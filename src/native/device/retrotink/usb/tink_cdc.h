#ifndef TINK_CDC_H
#define TINK_CDC_H

#include <stdbool.h>
#include <stdint.h>

#define TINK_CDC_INPUT_NAME_LENGTH 48

typedef struct {
    bool online;

    bool input_valid;
    int input_index;
    int input_ic;
    int input_model;
    char input_name[TINK_CDC_INPUT_NAME_LENGTH];

    bool output_valid;
    int output_selected;
    int output_live;
    int output_model;
} tink_cdc_state_t;

void tink_cdc_init(void);
void tink_cdc_task(void);

bool tink_cdc_is_connected(void);
bool tink_cdc_is_online(void);

/* Copy the latest reported device state into a caller-owned snapshot. */
void tink_cdc_get_state(tink_cdc_state_t *state);

/* Queue the explicit wakeup command while CDC is mounted but offline. */
bool tink_cdc_wakeup(void);

/* Queue an ordinary command only while the device is online. */
bool tink_cdc_send_command(const char *command);

#endif /* TINK_CDC_H */