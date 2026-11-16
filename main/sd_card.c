#include "sd_card.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"

#include "esp_vfs_fat.h"
#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"

#define TAG "SD_CARD"


esp_err_t sd_card_init(const char *mount_point)
{
    ESP_LOGI(TAG, "Initializing SD card");

    // Allocate memory for sdmmc_card_t
    sdmmc_card_t *card = NULL;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_MOSI,
        .miso_io_num = SD_MISO,
        .sclk_io_num = SD_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    // check for errors while initializing the SPI bus
    // for some reason my esp32 boot looped when SPI1 was used
    esp_err_t ret = spi_bus_initialize(
        SD_SPI_HOST,
        &bus_cfg,
        SPI_DMA_CH_AUTO
    );
    ESP_ERROR_CHECK(ret);

    // host configuration for SD card
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_SPI_HOST;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();

    // define communication pins
    slot_config.gpio_cs = SD_CS;
    slot_config.host_id = SD_SPI_HOST;


    ESP_LOGI(TAG, "Mounting filesystem");

    // mount the filesystem
     esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = MAX_FILES,
        .allocation_unit_size = 16 * 1024
    };

    ret = esp_vfs_fat_sdspi_mount(
        mount_point,
        &host,
        &slot_config,
        &mount_config,
        &card
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount filesystem: %s", esp_err_to_name(ret));
        free(card);
        return ESP_FAIL;
    }

    return ESP_OK;
}

void sd_card_deinit(const char *mount_point)
{
    ESP_LOGI(TAG, "Unmounting filesystem");
    esp_vfs_fat_sdcard_unmount(mount_point, NULL);
    ESP_LOGI(TAG, "Filesystem unmounted");
}

void readfile(const char *file_path)
{
    FILE *file = fopen(file_path, "r");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading: %s", file_path);
        return;
    }

    char line[128];
    while (fgets(line, sizeof(line), file)) {
        // Remove newline character if present
        line[strcspn(line, "\n")] = 0;
        ESP_LOGI(TAG, "Read line: %s", line);
    }

    fclose(file);
}

FILE *readfile_open(const char *file_path)
{
    FILE *file = fopen(file_path, "r");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading: %s", file_path);
        return NULL;
    }
    
    return file;
}

esp_err_t get_data(int index, int byte_count, const char *file_path, uint8_t *buffer)
{

    // get file from path
    FILE *file = readfile_open(file_path);
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file for reading while getting data: %s", file_path);
        return ESP_FAIL;
    }

    //cast index to long to avoid warning
    long offset = (long)index * byte_count;

    if (fseek(file, offset, SEEK_SET) != 0)
    {
        fclose(file);
        ESP_LOGE(TAG, "Failed to seek to offset %ld in file: %s", offset, file_path);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Reading %d bytes from file: %s at offset %ld", byte_count, file_path, offset);

    size_t bytes_read = fread(buffer, 1, byte_count, file);

    fclose(file);

    return ESP_OK;
}


uint8_t *get_buffer(int index, int buffer_size, const char *file_path)
{
    // callers job to free the buffer after use
    uint8_t *buffer = malloc(buffer_size * sizeof(*buffer));
    
    esp_err_t err = get_data(index, buffer_size, file_path, buffer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read data from file: %s", file_path);
        free(buffer);
        return NULL;
    }

    if (buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for buffer");
        return NULL;
    }
    
    return buffer;
}