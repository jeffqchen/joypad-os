#include "../retrotink_config.h"
#include "tink_cdc.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>

#include "tusb.h"
#include "usb/usbh/usbh.h"
#include "platform/platform.h"

#define TINK_RX_LINE_SIZE            160

/* User command queue */
#define TINK_COMMAND_QUEUE_SIZE  8
#define TINK_COMMAND_MAX_LENGTH  32

typedef enum {
    TINK_QUERY_INPUT,
    TINK_QUERY_OUTPUT
} tink_query_t;

/* Connection and device state */
static int retrotink_cdc_index = -1;
static tink_cdc_state_t retrotink_state;

/* Receive parser */
static char tink_rx_line[TINK_RX_LINE_SIZE];
static size_t tink_rx_line_length;

/* Heartbeat scheduler */
static tink_query_t tink_next_query = TINK_QUERY_INPUT;
static bool tink_query_pending;
static tink_query_t tink_pending_query;
static uint32_t tink_query_deadline;
static uint32_t tink_next_query_time;
static uint32_t tink_next_command_time;
static size_t tink_query_tx_offset;

/* User command queue */
static char tink_command_queue[TINK_COMMAND_QUEUE_SIZE]
                              [TINK_COMMAND_MAX_LENGTH];

static uint8_t tink_command_queue_head;
static uint8_t tink_command_queue_tail;
static uint8_t tink_command_queue_count;

static bool tink_cdc_send_queued_command(uint32_t now);

/* Tracks partial transmission of the current queued command. */
static size_t tink_command_tx_offset;

/* ---------------------------------------------------------
 * Internal helpers
 * --------------------------------------------------------- */

static void tink_query_complete(void)
{
    uint32_t now = platform_time_ms();

    if (!tink_query_pending) {
        return;
    }

    tink_query_pending = false;

    /* Alternate between input and output queries. */
    tink_next_query = (tink_pending_query == TINK_QUERY_INPUT)
                    ? TINK_QUERY_OUTPUT
                    : TINK_QUERY_INPUT;

    retrotink_state.online = true;

    tink_next_query_time = now + TINK_HEARTBEAT_INTERVAL_MS;
}

static bool tink_only_trailing_whitespace(const char *text)
{
    while (*text != '\0') {
        if (!isspace((unsigned char)*text)) {
            return false;
        }
        text++;
    }

    return true;
}

static void tink_process_response_line(const char *line)
{
    const char *payload;
    int value1, value2, value3;

    if (strncmp(line, "[COM] ", 6) != 0) {
        return;
    }

    payload = line + 6;

    /* A response is useful for heartbeat only if it matches the query
     * currently awaiting a response. Unsolicited command acknowledgements
     * (including the pwr on acknowledgement) are not proof of readiness.
     */
    if (!tink_query_pending) {
        printf("[RetroTINK] Ignored unsolicited response: %s\n", payload);
        return;
    }

    if (strcmp(payload, "input: busy") == 0) {
        if (tink_pending_query == TINK_QUERY_INPUT) {
            printf("[RetroTINK] Input query busy; device is responding\n");
            tink_query_complete();
        } else {
            printf("[RetroTINK] Ignored response for non-pending input query\n");
        }
        return;
    }

    if (strcmp(payload, "output: busy") == 0) {
        if (tink_pending_query == TINK_QUERY_OUTPUT) {
            printf("[RetroTINK] Output query busy; device is responding\n");
            tink_query_complete();
        } else {
            printf("[RetroTINK] Ignored response for non-pending output query\n");
        }
        return;
    }

    if (strncmp(payload, "input=", 6) == 0) {
        const char *name_start;
        const char *ic_field;
        size_t name_length;

        if (tink_pending_query != TINK_QUERY_INPUT) {
            printf("[RetroTINK] Ignored input response while output query is pending\n");
            return;
        }

        {
            int consumed = 0;

            if (sscanf(payload + 6, "%d%n", &value1, &consumed) != 1 ||
                consumed == 0 || !isspace((unsigned char)payload[6 + consumed])) {
                printf("[RetroTINK] Could not parse input response: %s\n", line);
                return;
            }

            name_start = payload + 6 + consumed;
        }

        while (isspace((unsigned char)*name_start)) name_start++;

        ic_field = strstr(name_start, " ic=");
        if (!ic_field || ic_field == name_start) {
            printf("[RetroTINK] Incomplete input response: %s\n", line);
            return;
        }

        name_length = (size_t)(ic_field - name_start);
        if (name_length >= sizeof(retrotink_state.input_name)) {
            printf("[RetroTINK] Input name too long: %s\n", line);
            return;
        }

        {
            int consumed = 0;
            if (sscanf(ic_field, " ic=%d model=%d %n",
                       &value2, &value3, &consumed) != 2 ||
                !tink_only_trailing_whitespace(ic_field + consumed)) {
                printf("[RetroTINK] Could not parse input metadata: %s\n", line);
                return;
            }
        }

        memcpy(retrotink_state.input_name, name_start, name_length);
        retrotink_state.input_name[name_length] = '\0';
        retrotink_state.input_index = value1;
        retrotink_state.input_ic = value2;
        retrotink_state.input_model = value3;
        retrotink_state.input_valid = true;

        tink_query_complete();

        printf("[RetroTINK] RetroTINK input: %d %s (ic=%d, model=%d)\n",
               retrotink_state.input_index,
               retrotink_state.input_name,
               retrotink_state.input_ic,
               retrotink_state.input_model);
        return;
    }

    if (strncmp(payload, "output sel=", 11) == 0) {
        if (tink_pending_query != TINK_QUERY_OUTPUT) {
            printf("[RetroTINK] Ignored output response while input query is pending\n");
            return;
        }

        {
            int consumed = 0;
            if (sscanf(payload, "output sel=%d live=%d model=%d %n",
                       &value1, &value2, &value3, &consumed) == 3 &&
                tink_only_trailing_whitespace(payload + consumed)) {
                retrotink_state.output_selected = value1;
                retrotink_state.output_live = value2;
                retrotink_state.output_model = value3;
                retrotink_state.output_valid = true;

                tink_query_complete();

                printf("[RetroTINK] RetroTINK output: selected=%d live=%d model=%d\n",
                       retrotink_state.output_selected,
                       retrotink_state.output_live,
                       retrotink_state.output_model);
                return;
            }
        }

        printf("[RetroTINK] Could not parse output response: %s\n", line);
        return;
    }

    printf("[RetroTINK] Ignored unrelated COM response: %s\n", payload);
}

static void tink_cdc_clear_state(void)
{
    memset(&retrotink_state, 0, sizeof(retrotink_state));
}

static void tink_cdc_clear_command_queue(void)
{
    tink_command_queue_head = 0;
    tink_command_queue_tail = 0;
    tink_command_queue_count = 0;
    tink_command_tx_offset = 0;
}

/* On loss of heartbeat, discard stale user actions but retain an explicit
 * wakeup requested while the heartbeat was still pending.
 */
static void tink_cdc_discard_ordinary_commands(void)
{
    char wakeup_queue[TINK_COMMAND_QUEUE_SIZE][TINK_COMMAND_MAX_LENGTH];
    uint8_t wakeup_count = 0;

    for (uint8_t i = 0; i < tink_command_queue_count; i++) {
        uint8_t index = (uint8_t)(
            (tink_command_queue_tail + i) % TINK_COMMAND_QUEUE_SIZE
        );

        if (strcmp(tink_command_queue[index], "pwr on") == 0 &&
            wakeup_count < TINK_COMMAND_QUEUE_SIZE) {
            memcpy(wakeup_queue[wakeup_count],
                   tink_command_queue[index],
                   TINK_COMMAND_MAX_LENGTH);
            wakeup_count++;
        }
    }

    tink_cdc_clear_command_queue();

    for (uint8_t i = 0; i < wakeup_count; i++) {
        memcpy(tink_command_queue[i], wakeup_queue[i],
               TINK_COMMAND_MAX_LENGTH);
    }

    tink_command_queue_tail = 0;
    tink_command_queue_head = (uint8_t)(wakeup_count % TINK_COMMAND_QUEUE_SIZE);
    tink_command_queue_count = wakeup_count;
}

static bool tink_cdc_wakeup_is_queued(void)
{
    for (uint8_t i = 0; i < tink_command_queue_count; i++) {
        uint8_t index = (uint8_t)(
            (tink_command_queue_tail + i) % TINK_COMMAND_QUEUE_SIZE
        );

        if (strcmp(tink_command_queue[index], "pwr on") == 0) {
            return true;
        }
    }

    return false;
}

static void tink_rx_feed(const uint8_t *data, uint32_t length)
{
    for (uint32_t i = 0; i < length; i++) {
        char ch = (char)data[i];

        if (ch == '\r' || ch == '\n') {
            if (tink_rx_line_length > 0) {
                tink_rx_line[tink_rx_line_length] = '\0';
                tink_process_response_line(tink_rx_line);
                tink_rx_line_length = 0;
            }
            continue;
        }

        if (tink_rx_line_length < sizeof(tink_rx_line) - 1) {
            tink_rx_line[tink_rx_line_length++] = ch;
        } else {
            tink_rx_line_length = 0;
            printf("[RetroTINK] Discarded overlong RetroTINK response line\n");
        }
    }
}

/* ---------------------------------------------------------
 * TinyUSB CDC callbacks
 * --------------------------------------------------------- */

void tuh_cdc_mount_cb(uint8_t idx)
{
    tuh_itf_info_t info;
    uint16_t vid = 0;
    uint16_t pid = 0;

    if (!tuh_cdc_itf_get_info(idx, &info)) {
        return;
    }

    if (!tuh_vid_pid_get(info.daddr, &vid, &pid)) {
        return;
    }

    if (vid == RETROTINK_USB_VID && pid == RETROTINK_USB_PID) {
        retrotink_cdc_index = idx;

        memset(&retrotink_state, 0, sizeof(retrotink_state));
        tink_rx_line_length = 0;

        tink_query_pending = false;
        tink_query_tx_offset = 0;
        tink_pending_query = TINK_QUERY_INPUT;
        tink_next_query = TINK_QUERY_INPUT;

        tink_cdc_clear_command_queue();

        tink_next_query_time = platform_time_ms();
        tink_next_command_time = platform_time_ms();

        printf("[RetroTINK] RetroTINK USB serial connected\n");
        printf("[RetroTINK] Device address: %u, CDC index: %u, interface: %u\n",
               info.daddr, idx, info.desc.bInterfaceNumber);
    }
}

void tuh_cdc_umount_cb(uint8_t idx)
{
    if ((int)idx == retrotink_cdc_index) {
        printf("[RetroTINK] RetroTINK USB serial disconnected\n");

        retrotink_cdc_index = -1;

        memset(&retrotink_state, 0, sizeof(retrotink_state));
        tink_rx_line_length = 0;

        tink_query_pending = false;
        tink_query_tx_offset = 0;

        tink_cdc_clear_command_queue();

        tink_pending_query = TINK_QUERY_INPUT;
    }
}

void tuh_cdc_rx_cb(uint8_t idx)
{
    uint8_t buffer[64];

    if ((int)idx != retrotink_cdc_index) {
        return;
    }

    while (tuh_cdc_read_available(idx) > 0) {
        uint32_t available = tuh_cdc_read_available(idx);
        uint32_t count = available;

        if (count > sizeof(buffer)) {
            count = sizeof(buffer);
        }

        uint32_t received = tuh_cdc_read(idx, buffer, count);

        if (received == 0) {
            break;
        }

        tink_rx_feed(buffer, received);
    }
}

/* ---------------------------------------------------------
 * Public API
 * --------------------------------------------------------- */

void tink_cdc_init(void)
{
    retrotink_cdc_index = -1;

    memset(&retrotink_state, 0, sizeof(retrotink_state));

    tink_rx_line_length = 0;

    tink_query_pending = false;
    tink_query_tx_offset = 0;
    tink_pending_query = TINK_QUERY_INPUT;
    tink_next_query = TINK_QUERY_INPUT;

    tink_cdc_clear_command_queue();

    tink_next_query_time = 0;
    tink_next_command_time = 0;
}

bool tink_cdc_is_connected(void)
{
    return retrotink_cdc_index >= 0;
}

bool tink_cdc_is_online(void)
{
    return retrotink_state.online;
}

void tink_cdc_get_state(tink_cdc_state_t *state)
{
    if (state != NULL) {
        *state = retrotink_state;
    }
}

static bool tink_cdc_queue_command(const char *command)
{
    if (retrotink_cdc_index < 0 || command == NULL) {
        return false;
    }

    size_t length = strlen(command);

    if (length == 0 ||
        length >= TINK_COMMAND_MAX_LENGTH) {
        return false;
    }

    if (tink_command_queue_count >=
        TINK_COMMAND_QUEUE_SIZE) {
        printf("[RetroTINK] Command queue full\n");
        return false;
    }

    memcpy(
        tink_command_queue[tink_command_queue_head],
        command,
        length + 1
    );

    tink_command_queue_head++;

    if (tink_command_queue_head >=
        TINK_COMMAND_QUEUE_SIZE) {
        tink_command_queue_head = 0;
    }

    tink_command_queue_count++;

    printf("[RetroTINK] Queued: %s\n", command);

    return true;
}

bool tink_cdc_wakeup(void)
{
    if (retrotink_cdc_index < 0 || retrotink_state.online) {
        return false;
    }

    if (tink_cdc_wakeup_is_queued()) {
        return true;
    }

    return tink_cdc_queue_command("pwr on");
}

bool tink_cdc_send_command(const char *command)
{
    if (!retrotink_state.online) {
        return false;
    }

    return tink_cdc_queue_command(command);
}

static bool tink_cdc_send_queued_command(uint32_t now)
{
    if (tink_command_queue_count == 0) {
        return false;
    }

    /* Do not start a new command during a heartbeat query. */
    if (tink_command_tx_offset == 0 && tink_query_pending) {
        return false;
    }

    /* Enforce the command interval before starting a new command. */
    if (tink_command_tx_offset == 0 &&
        (int32_t)(now - tink_next_command_time) < 0) {
        return false;
    }

    const char *command =
        tink_command_queue[tink_command_queue_tail];

    size_t length = strlen(command);
    size_t total_length = length + 1;

    char tx_buffer[TINK_COMMAND_MAX_LENGTH + 1];

    memcpy(tx_buffer, command, length);
    tx_buffer[length] = '\n';

    size_t remaining =
        total_length - tink_command_tx_offset;

    uint32_t written = tuh_cdc_write(
        (uint8_t)retrotink_cdc_index,
        tx_buffer + tink_command_tx_offset,
        remaining
    );

    if (written == 0) {
        return false;
    }

    tink_command_tx_offset += written;

    tuh_cdc_write_flush((uint8_t)retrotink_cdc_index);

    /* Keep the command queued until every byte is transmitted. */
    if (tink_command_tx_offset < total_length) {
        return true;
    }

    tink_command_queue_tail++;

    if (tink_command_queue_tail >=
        TINK_COMMAND_QUEUE_SIZE) {
        tink_command_queue_tail = 0;
    }

    tink_command_queue_count--;
    tink_command_tx_offset = 0;

    tink_next_command_time =
        now + TINK_COMMAND_INTERVAL_MS;

    printf("[RetroTINK] Sent: %s\n", command);

    return true;
}

static bool tink_cdc_send_heartbeat_query(uint32_t now)
{
    const char *command;
    size_t command_length;

    if (tink_next_query == TINK_QUERY_INPUT) {
        command = "input\n";
        command_length = sizeof("input\n") - 1;
    } else {
        command = "output\n";
        command_length = sizeof("output\n") - 1;
    }

    size_t remaining =
        command_length - tink_query_tx_offset;

    uint32_t written = tuh_cdc_write(
        (uint8_t)retrotink_cdc_index,
        command + tink_query_tx_offset,
        (uint32_t)remaining
    );

    if (written == 0) {
        return false;
    }

    tink_query_tx_offset += written;

    tuh_cdc_write_flush((uint8_t)retrotink_cdc_index);

    /* Wait until the entire query has been submitted. */
    if (tink_query_tx_offset < command_length) {
        return true;
    }

    tink_query_tx_offset = 0;

    tink_pending_query = tink_next_query;
    tink_query_pending = true;

    tink_query_deadline = now + TINK_QUERY_TIMEOUT_MS;
    tink_next_command_time =
        now + TINK_COMMAND_INTERVAL_MS;

    printf("[RetroTINK] Sent heartbeat query: %s", command);

    return true;
}

void tink_cdc_task(void)
{
    uint32_t now = platform_time_ms();

    if (retrotink_cdc_index < 0) {
        return;
    }

    if (tink_query_pending) {
        if ((int32_t)(now - tink_query_deadline) >= 0) {
            printf("[RetroTINK] RetroTINK heartbeat timeout\n");

            tink_query_pending = false;
            tink_next_query = (tink_pending_query == TINK_QUERY_INPUT)
                            ? TINK_QUERY_OUTPUT
                            : TINK_QUERY_INPUT;
            tink_cdc_clear_state();
            tink_cdc_discard_ordinary_commands();

            tink_next_query_time =
                now + TINK_HEARTBEAT_INTERVAL_MS;
        }

        return;
    }

    /*
    * Finish a partially transmitted heartbeat query first.
    */
    if (tink_query_tx_offset > 0) {
        tink_cdc_send_heartbeat_query(now);
        return;
    }

    /*
    * Finish a partially transmitted user command before
    * starting another heartbeat query.
    */
    if (tink_command_tx_offset > 0) {
        tink_cdc_send_queued_command(now);
        return;
    }

    if ((int32_t)(now - tink_next_query_time) < 0) {
        tink_cdc_send_queued_command(now);
        return;
    }

    if ((int32_t)(now - tink_next_command_time) < 0) {
        return;
    }

    tink_cdc_send_heartbeat_query(now);
    return;
}
