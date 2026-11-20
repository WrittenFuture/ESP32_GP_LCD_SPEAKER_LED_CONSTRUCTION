#pragma once

#include <stdint.h>
#include <stddef.h>

#include "esp_err.h"

typedef struct
{
    uint8_t *data;
    size_t size;

} buffer_u8_t;

typedef struct
{
    void *buffer;     // data buffer
    void *buffer_end; // end of data buffer
    size_t capacity;  // maximum number of items in the buffer
    size_t count;     // number of items in the buffer
    size_t sz;        // size of each item in the buffer
    void *head;       // pointer to head
    void *tail;       // pointer to tail
} circ_buff_t;

buffer_u8_t get_buffer(size_t size);
void print_buffer(buffer_u8_t *buffer);

circ_buff_t cb_init(size_t capacity, size_t sz);
esp_err_t circ_buff_push(circ_buff_t *cb, const void *item);
esp_err_t crc_buff_pop(circ_buff_t *cb, void *item);
size_t get_circ_buff_size(circ_buff_t *cb);
