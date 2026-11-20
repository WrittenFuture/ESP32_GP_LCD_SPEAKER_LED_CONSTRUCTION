#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_log.h"

#include "util.h"

static const char *TAG = "UTIL";

// buffers
buffer_u8_t get_buffer(size_t size)
{
    //caller should free this
    uint8_t *data = malloc(size);
    if (data == NULL)
    {
        ESP_LOGE(TAG, "failed to allocate buffer");
        ESP_LOGW(TAG, "needed %zu bytes in memory had %u with the largest block of %u bytes", 
            size,
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT), 
            (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
            
        }
    
    buffer_u8_t buffer = {.data = data, .size = size};

    return buffer;
}

void print_buffer(buffer_u8_t *buffer)
{
    // Print the bytes of the buffer in hex
    for (int i = 0; i < buffer->size; i++)
    {
        printf("%02X ", buffer->data[i]);

        if ((i + 1) % 16 == 0)
        {
            printf("\n");
        }
    }

    printf("\n");
}

// circular buffers

circ_buff_t cb_init(size_t capacity, size_t sz)
{

    //caller needs to free
    size_t tot_size = capacity * sz;
    void *buffer = malloc(tot_size);
    if (buffer == NULL)
    {
        ESP_LOGE(TAG, "failed to allocate buffer");
        ESP_LOGW(TAG, "needed %zu bytes in memory had %u with the largest block of %u bytes", 
            tot_size,
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT), 
            (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    }

    circ_buff_t cb =
    {
        .buffer = buffer,
        .buffer_end = buffer + tot_size,
        .capacity = capacity,
        .count = 0,
        .sz = sz,
        .head = buffer,
        .tail = buffer,
    };

    return cb;
}

esp_err_t circ_buff_push(circ_buff_t *cb, const void *item)
{
    if(cb->count == cb->capacity){
        ESP_LOGW(TAG, "Circular buffer overflow");
        return ESP_ERR_INVALID_STATE;
    }

    // copy item onto head
    memcpy(cb->head, item, cb->sz);

    // move head forward the size of the item
    cb->head = (char*)cb->head + cb->sz;

    if(cb->head == cb->buffer_end)
    {
        cb->head = cb->buffer;
    }
    
    cb->count++;
    return ESP_OK;
}

esp_err_t crc_buff_pop(circ_buff_t *cb, void *item)
{
    if(cb->count == 0){
        ESP_LOGW(TAG, "circular buffer underflow");
        return ESP_ERR_INVALID_STATE;
    }

    // copy the item on tail to the passed pointer
    memcpy(item, cb->tail, cb->sz);

    //move tail to new last item
    cb->tail = (char*)cb->tail + cb->sz;

    if(cb->tail == cb->buffer_end)
    {
        cb->tail = cb->buffer;
    }

    cb->count--;
    return ESP_OK;
}

size_t get_circ_buff_size(circ_buff_t *cb)
{
    return cb->count * cb->sz;
}