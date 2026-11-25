#include "sd_card.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "esp_vfs_fat.h"
#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "util.h"

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
        SPI_DMA_CH_AUTO);
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
        .allocation_unit_size = 16 * 1024};

    ret = esp_vfs_fat_sdspi_mount(
        mount_point,
        &host,
        &slot_config,
        &mount_config,
        &card);

    if (ret != ESP_OK)
    {
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
    if (file == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file for reading: %s", file_path);
        return;
    }

    char line[128];
    while (fgets(line, sizeof(line), file))
    {
        // Remove newline character if present
        line[strcspn(line, "\n")] = 0;
        ESP_LOGI(TAG, "Read line: %s", line);
    }

    fclose(file);
}

FILE *readfile_open(const char *file_path)
{
    FILE *file = fopen(file_path, "rb");
    if (file == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file for reading: %s", file_path);
        return NULL;
    }

    return file;
}

// TODO: add safety checks
esp_err_t copy_data(buffer_u8_t *buffer, size_t buffer_offset, size_t file_start, size_t byte_count, const char *file_path)
{
    if (buffer->data == NULL)
    {
        ESP_LOGE(TAG, "buffer is null");
        return ESP_ERR_INVALID_SIZE;
    }
    if (buffer->size < byte_count)
    {
        ESP_LOGE(TAG, "Buffer is to small");
        return ESP_ERR_INVALID_SIZE;
    }

    // get file from path
    FILE *file = readfile_open(file_path);
    if (!file)
    {
        ESP_LOGE(TAG, "Failed to open file for reading while getting data: %s", file_path);
        return ESP_FAIL;
    }

    // set cursor to where i want file to start reading
    if (fseek(file, file_start, SEEK_SET) != 0)
    {
        fclose(file);
        ESP_LOGE(TAG, "Failed to seek to offset %ld in file: %s", file_start, file_path);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Reading %d bytes from file: %s at offset %ld", byte_count, file_path, file_start);

    // copy bytes into buffer from file_start
    fread(buffer->data + buffer_offset, 1, byte_count, file);

    fclose(file);

    return ESP_OK;
}

esp_err_t get_data(int index, int byte_count, const char *file_path, buffer_u8_t *buffer)
{

    if (buffer == NULL || buffer->data == NULL)
    {
        ESP_LOGE(TAG, "audio buffer is null");
        return ESP_ERR_INVALID_ARG;
    }

    if (byte_count > buffer->size)
    {
        ESP_LOGE(TAG, "buffer is too small");
        return ESP_ERR_INVALID_SIZE;
    }

    // get file from path
    FILE *file = readfile_open(file_path);
    if (!file)
    {
        ESP_LOGE(TAG, "Failed to open file for reading while getting data: %s", file_path);
        return ESP_FAIL;
    }

    // cast index to long to avoid warning
    long offset = (long)index * byte_count;

    if (fseek(file, offset, SEEK_SET) != 0)
    {
        fclose(file);
        ESP_LOGE(TAG, "Failed to seek to offset %ld in file: %s", offset, file_path);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Reading %d bytes from file: %s at offset %ld", byte_count, file_path, offset);

    size_t bytes_read = fread(buffer->data, 1, byte_count, file);
    if (bytes_read == 0)
    {
        fclose(file);
        ESP_LOGE(TAG, "Failed to read %d bytes from %s", byte_count, file_path);
        return ESP_FAIL;
    }

    fclose(file);

    return ESP_OK;
}

void stream_data(void *pvParameters)
{

    ESP_LOGI(TAG, "stream_data running on core %d", xPortGetCoreID());

    stream_data_params *params = (stream_data_params *)pvParameters;

    buffer_u8_t temp_sd_data = get_buffer(params->copy_size);
    if (temp_sd_data.data == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate SD buffer");
        free(temp_sd_data.data);
        vTaskDelete(NULL);
    }

    bool pushed = true;
    bool end_of_file = false;
    int counter = 0;

    while (1)
    {
        if (pushed)
        {
            esp_err_t ret = copy_data(
                &temp_sd_data,
                0,
                counter * params->copy_size,
                params->copy_size,
                params->file_path);

            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, " failed to copy sd data while streaming retrying");
                continue;
            }
        }

        if (params->raw_data_queue == NULL)
        {
            ESP_LOGE(TAG, "queue null");
            free(temp_sd_data.data);
            vTaskDelete(NULL);
        }

        ESP_LOGI(TAG, "Sending data to decoder");
        if (xQueueSend(params->raw_data_queue, temp_sd_data.data, portMAX_DELAY) == pdPASS)
        {
            pushed = true;
            counter++;
        }
        else
        {
            ESP_LOGE(TAG, " failed to push sd data while streaming retrying");
            pushed = false;
        }
        ESP_LOGI(TAG, "finished Sending data to decoder");

        // TODO: flip true when EOF is reached
        if (end_of_file)
        {
            break;
        }
    }
    free(temp_sd_data.data);
    vTaskDelete(NULL);
}