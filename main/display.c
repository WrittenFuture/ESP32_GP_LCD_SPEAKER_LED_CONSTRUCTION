#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "esp_lcd_io_i80.h"
#include "esp_lcd_panel_io.h"

#include "display.h"

static const char *TAG = "DISPLAY";

buffer_u8_t display_init(void)
{
    // ------------------------------------------------
    // GPIO CONFIGURATION
    // ------------------------------------------------

    ESP_ERROR_CHECK(gpio_reset_pin(LCD_RST));
    ESP_ERROR_CHECK(gpio_set_direction(LCD_RST, GPIO_MODE_OUTPUT));

    ESP_ERROR_CHECK(gpio_reset_pin(LCD_CS));
    ESP_ERROR_CHECK(gpio_set_direction(LCD_CS, GPIO_MODE_OUTPUT));

    //ESP_ERROR_CHECK(gpio_reset_pin(LCD_RD));
    //ESP_ERROR_CHECK(gpio_set_direction(LCD_RD, GPIO_MODE_OUTPUT));

    ESP_ERROR_CHECK(gpio_set_level(LCD_RST, 1));
    ESP_ERROR_CHECK(gpio_set_level(LCD_CS, 1));

    // RD is active-low. Keep reads disabled.
    //ESP_ERROR_CHECK(gpio_set_level(LCD_RD, 1));

    // ------------------------------------------------
    // CREATE I80 BUS
    // ------------------------------------------------

    esp_lcd_i80_bus_config_t bus_config = {
        .dc_gpio_num = LCD_RS,
        .wr_gpio_num = LCD_WR,

        .data_gpio_nums = {
            LCD_D0,
            LCD_D1,
            LCD_D2,
            LCD_D3,
            LCD_D4,
            LCD_D5,
            LCD_D6,
            LCD_D7},

        .bus_width = 8,
        .max_transfer_bytes = DISPLAY_BUFFER_SIZE,
        .dma_burst_size = 64,
    };

    ESP_ERROR_CHECK(
        esp_lcd_new_i80_bus(&bus_config, &lcd_bus));

    // ------------------------------------------------
    // CREATE LCD PANEL IO
    // ------------------------------------------------

    esp_lcd_panel_io_i80_config_t io_config = {
        .cs_gpio_num = LCD_CS,
        .pclk_hz = 2000000,
        .trans_queue_depth = 1,

        .dc_levels = {
            .dc_cmd_level = 0,
            .dc_data_level = 1,
        },

        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };

    ESP_ERROR_CHECK(
        esp_lcd_new_panel_io_i80(
            lcd_bus,
            &io_config,
            &lcd_io));

    ESP_LOGI(TAG, "Resetting LCD");

    ESP_ERROR_CHECK(gpio_set_level(LCD_RST, 0));
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_ERROR_CHECK(gpio_set_level(LCD_RST, 1));
    vTaskDelay(pdMS_TO_TICKS(150));

    ESP_LOGI(TAG, "Initializing controller");

    // these commands have 0 bytes of parameters
    // send soft reset command to display
    ESP_LOGI(TAG, "Sending software reset command");
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(lcd_io, 0x01, NULL, 0));
    vTaskDelay(pdMS_TO_TICKS(150));

    // Exit sleep; wait ≥120 ms
    ESP_LOGI(TAG, "Sending exit sleep command");
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(lcd_io, 0x11, NULL, 0));
    vTaskDelay(pdMS_TO_TICKS(150));

    // 0x55 = RGB565
    ESP_LOGI(TAG, "Sending pixel format");
    uint8_t pixel_format[] = {0x55};
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(lcd_io, 0x3A, pixel_format, sizeof(pixel_format)));

    // Memory access control: rotation, BGR/RGB, h/v mirror
    /*
        0° (portrait) 	    0x48
        90° (landscape) 	0x28
        180° (portrait) 	0x88
        270° (landscape) 	0xE8
    */
    ESP_LOGI(TAG, "Sending orientation");
    uint8_t orientation[] = {0x28};
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(lcd_io, 0x36, orientation, sizeof(orientation)));

    // Turn off inversion
    ESP_LOGI(TAG, "Disabling inversion ");
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(lcd_io, 0x20, NULL, 0));

    // Turn on display
    // use 0x28 to turn off display
    ESP_LOGI(TAG, "Enabling display");
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(lcd_io, 0x29, NULL, 0));

    // allocate a buffer that is DMA capeable
    uint8_t *buffer = heap_caps_malloc(
        DISPLAY_BUFFER_SIZE,
        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (buffer == NULL)
    {
        ESP_LOGE(TAG, "DMA allocation failed");
        buffer_u8_t ret = {.data = NULL, .size = 0};
        return ret;
    }

    buffer_u8_t ret = {.data = buffer, .size = DISPLAY_BUFFER_SIZE};
    return ret;
}

void display_flush(uint16_t color, buffer_u8_t display_buff)
{
    ESP_LOGI(TAG, "Displaying RGB565: 0x%04X", (unsigned int)color);

    // Fill DMA buffer with RGB565 pixels.
    for (int i = 0; i < DISPLAY_BUFFER_SIZE; i += 2)
    {
        // splits color into two u8
        display_buff.data[i] = (color >> 8) & 0xFF;
        display_buff.data[i + 1] = color & 0xFF;
    }

    // Draw the entire display in chunks.
    for (int y = 0; y < LCD_HEIGHT; y += BUFFER_LINES)
    {

        // start x_high, start x_low, end.., end..
        // start: 0x0000, end: 0x01df(479)
        // this means all 480 pixels of the width
        uint8_t x_addr[] = {
            0x00, 0x00,
            0x01, 0xDF};

        // same as x but with changing y's
        uint8_t y_addr[] = {
            // start adress
            (y >> 8) & 0xFF,
            y & 0xFF,

            // end adress
            ((y + BUFFER_LINES - 1) >> 8) & 0xFF,
            (y + BUFFER_LINES - 1) & 0xFF};

        // Set column address.
        ESP_ERROR_CHECK(
            esp_lcd_panel_io_tx_param(
                lcd_io, 0x2A, x_addr, sizeof(x_addr)));

        // Set page address.
        ESP_ERROR_CHECK(
            esp_lcd_panel_io_tx_param(
                lcd_io, 0x2B, y_addr, sizeof(y_addr)));

        // Send pixel data.
        ESP_ERROR_CHECK(
            esp_lcd_panel_io_tx_color(
                lcd_io,
                0x2C,
                display_buff.data,
                display_buff.size));


        // Queue depth is one. The next command is queued
        // after the color transfer, so it acts as a
        // synchronization point before buffer reuse.
    }

    // Wait for the last queued transfer to finish.
    ESP_ERROR_CHECK(
        esp_lcd_panel_io_tx_param(
            lcd_io, 0x00, NULL, 0));

    vTaskDelay(pdMS_TO_TICKS(1000));
}

void display_off(void)
{
    ESP_LOGI(TAG, "Enabling display");
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(lcd_io, 0x28, NULL, 0));

}

void display_on(void)
{
    ESP_LOGI(TAG, "Enabling display");
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(lcd_io, 0x29, NULL, 0));
}