#pragma once

#define SD_MISO GPIO_NUM_19
#define SD_MOSI GPIO_NUM_23
#define SD_CLK  GPIO_NUM_18
#define SD_CS   GPIO_NUM_5

#define SD_SPI_HOST 1
#define MOUNT_POINT1 "/sdcard"
#define MOUNT_POINT2 "/sdcard2"
#define MAX_FILES 5

// this is where file paths are stored
#define test_FILE_PATH "/sdcard/test.txt"
// this is where mp3 files are stored
#define MP3_FILE_PATH "/sdcard/music/"
// 4k buffer size for reading files

// functions
#include "esp_err.h"

esp_err_t sd_card_init(const char *mount_point);
void readfile(const char *file_path);
FILE *readfile_open(const char *file_path);
uint8_t *get_buffer(int index, int buffer_size, const char *file_path);
esp_err_t get_data(int index, int byte_count, const char *file_path, uint8_t *buffer);