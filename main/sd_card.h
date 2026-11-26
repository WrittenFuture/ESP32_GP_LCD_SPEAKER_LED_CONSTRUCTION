#pragma once

#include "util.h"
#include <stdio.h>

#define SD_MISO GPIO_NUM_19
#define SD_MOSI GPIO_NUM_23
#define SD_CLK  GPIO_NUM_18
#define SD_CS   GPIO_NUM_5

#define SD_SPI_HOST 1
#define MOUNT_POINT1 "/sdcard"
#define MOUNT_POINT2 "/sdcard2"
#define MAX_FILES 5
#define NUM_MUSIC_FILES 57

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// this is where file paths are stored
#define test_FILE_PATH "/sdcard/test.txt"
// this is where mp3 files are stored
#define MP3_FILE_PATH "/sdcard/music/"
// 4k buffer size for reading files

// functions
#include "esp_err.h"

typedef struct
{
    QueueHandle_t raw_data_queue;
    size_t copy_size;
    const char *file_path;
    bool *eof;
} stream_data_params;

void stream_data(void *pvParameters);

esp_err_t sd_card_init(const char *mount_point);
void readfile(const char *file_path);
int dir_file_count(const char *directory);
FILE *readfile_open(const char *file_path);
esp_err_t copy_data(buffer_u8_t *buffer, size_t buffer_offset, size_t file_start, size_t byte_count, const char *file_path, bool *eof);
esp_err_t get_data(int index, int byte_count, const char *file_path, buffer_u8_t *buffer);