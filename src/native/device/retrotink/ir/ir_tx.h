#ifndef IR_TX_H
#define IR_TX_H

#include <stdbool.h>
#include <stdint.h>

void ir_tx_init(void);

bool ir_tx_enqueue(uint16_t command);

bool ir_tx_start_repeat(uint16_t command);
bool ir_tx_start_full_repeat(uint16_t command);
bool ir_tx_queue_full_repeat_frame(uint16_t command);

void ir_tx_stop_repeat(void);

void ir_tx_task(void);

#endif