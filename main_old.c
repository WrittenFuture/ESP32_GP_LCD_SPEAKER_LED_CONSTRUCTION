#include <stdio.h>
#include <string.h>
#include <math.h>


#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/uart.h"

#include "driver/i2s_std.h"

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "esp_lcd_io_i80.h"
#include "esp_lcd_panel_io.h"

#include <led_strip.h>
static led_strip_handle_t strip;

static const char *TAG = "SD_CARD";

#define ACTIVATE_PIN 35

#define PIN_NUM_MISO 19
#define PIN_NUM_MOSI 23
#define PIN_NUM_CLK  18
#define PIN_NUM_CS   5

#define I2S_BCLK 4
#define I2S_LRCK 0
#define I2S_DIN 15
#define AMP_SD 7

#define SAMPLE_RATE 44100
#define FREQUENCY 440
#define AMPLITUDE 16000

//#define LED_TYPE LED_STRIP_WS2812
#define LED_DATA  GPIO_NUM_21

//LCD pins
#define LCD_RST 16
#define LCD_CS 17
#define LCD_RS 3
#define LCD_WR 2
#define LCD_RD 22

#define LCD_D0 12
#define LCD_D1 13
#define LCD_D2 32
#define LCD_D3 33
#define LCD_D4 25
#define LCD_D5 26
#define LCD_D6 27
#define LCD_D7 14


typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} rgb_t;

static const rgb_t colors[] = {
    { .r = 0x0f, .g = 0x0f, .b = 0x0f }, // White
    { .r = 0x00, .g = 0x00, .b = 0x2f }, // Blue
    { .r = 0x00, .g = 0x2f, .b = 0x00 }, // Green
    { .r = 0x2f, .g = 0x00, .b = 0x00 }, // Red
    { .r = 0x00, .g = 0x00, .b = 0x00 }, // Off
};

#define COLORS_TOTAL (sizeof(colors) / sizeof(colors[0]))
#define LED_COUNT 14

#define UART_PORT UART_NUM_1
#define RX_PIN 36
#define TX_PIN UART_PIN_NO_CHANGE
#define BUF_SIZE 256

static const char *DIST_TAG = "DISTANCE_SENSOR";

void dist_test()
{
     uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT
    };

    // Install UART driver
    ESP_ERROR_CHECK(
        uart_driver_install(UART_PORT, BUF_SIZE * 2, 0, 0, NULL, 0)
    );

    // Configure UART
    ESP_ERROR_CHECK(
        uart_param_config(UART_PORT, &uart_config)
    );

    // Configure RX pin only
    ESP_ERROR_CHECK(
        uart_set_pin(
            UART_PORT,
            UART_PIN_NO_CHANGE,
            RX_PIN,
            UART_PIN_NO_CHANGE,
            UART_PIN_NO_CHANGE
        )
    );

    uint8_t data[BUF_SIZE];

    ESP_LOGI(DIST_TAG, "Listening for distance sensor data...");

    while (1) {
        int len = uart_read_bytes(
            UART_PORT,
            data,
            BUF_SIZE - 1,
            pdMS_TO_TICKS(1000)
        );

        if (len > 0) {
            data[len] = '\0';

            ESP_LOG_BUFFER_HEX(DIST_TAG, data, len);
            ESP_LOGI(DIST_TAG, "Received %d bytes", len);
        }
    }
}

void dist_task(void *pvParameters)
{
    dist_test();
    vTaskDelete(NULL);
    ESP_LOGI(DIST_TAG, "exiting dist task");
}

void print_input(int pin)
{
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    int value = gpio_get_level(pin);
    printf("GPIO %d: %d\n", pin, value);
}

void print_input_task(void *pvParameters)
{
    while (1)
    {
        print_input(ACTIVATE_PIN);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void lcd_test(void)
{
    ESP_LOGI(TAG, "Starting ILI9486 LCD test");

    #define LCD_WIDTH       480
    #define LCD_HEIGHT      320
    #define BUFFER_LINES    8
    #define BUFFER_SIZE     (LCD_WIDTH * BUFFER_LINES * 2)

    static esp_lcd_i80_bus_handle_t lcd_bus;
    static esp_lcd_panel_io_handle_t lcd_io;

    // ------------------------------------------------
    // GPIO CONFIGURATION
    // ------------------------------------------------

    ESP_ERROR_CHECK(gpio_reset_pin(LCD_RST));
    ESP_ERROR_CHECK(gpio_set_direction(LCD_RST, GPIO_MODE_OUTPUT));

    ESP_ERROR_CHECK(gpio_reset_pin(LCD_CS));
    ESP_ERROR_CHECK(gpio_set_direction(LCD_CS, GPIO_MODE_OUTPUT));

    ESP_ERROR_CHECK(gpio_reset_pin(LCD_RD));
    ESP_ERROR_CHECK(gpio_set_direction(LCD_RD, GPIO_MODE_OUTPUT));

    ESP_ERROR_CHECK(gpio_set_level(LCD_RST, 1));
    ESP_ERROR_CHECK(gpio_set_level(LCD_CS, 1));

    // RD is active-low. Keep reads disabled.
    ESP_ERROR_CHECK(gpio_set_level(LCD_RD, 1));

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
            LCD_D7
        },

        .bus_width = 8,
        .max_transfer_bytes = BUFFER_SIZE,
        .dma_burst_size = 64,
    };

    ESP_ERROR_CHECK(
        esp_lcd_new_i80_bus(&bus_config, &lcd_bus)
    );

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
            &lcd_io
        )
    );

    // ------------------------------------------------
    // HARDWARE RESET
    // ------------------------------------------------

    ESP_LOGI(TAG, "Resetting LCD");

    ESP_ERROR_CHECK(gpio_set_level(LCD_RST, 0));
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_ERROR_CHECK(gpio_set_level(LCD_RST, 1));
    vTaskDelay(pdMS_TO_TICKS(150));

    // ------------------------------------------------
    // COMMAND HELPERS
    // ------------------------------------------------

    #define LCD_CMD(cmd) \
        ESP_ERROR_CHECK( \
            esp_lcd_panel_io_tx_param(lcd_io, cmd, NULL, 0) \
        )

    #define LCD_DATA(cmd, data) \
        ESP_ERROR_CHECK( \
            esp_lcd_panel_io_tx_param( \
                lcd_io, cmd, data, sizeof(data) \
            ) \
        )

    // ------------------------------------------------
    // ILI9486 INITIALIZATION
    // ------------------------------------------------

    ESP_LOGI(TAG, "Initializing controller");

    // Software reset
    LCD_CMD(0x01);
    vTaskDelay(pdMS_TO_TICKS(150));

    // Sleep out
    LCD_CMD(0x11);
    vTaskDelay(pdMS_TO_TICKS(150));

    // Interface pixel format
    // 0x55 = RGB565
    uint8_t pixel_format[] = {0x55};
    LCD_DATA(0x3A, pixel_format);

    // Memory access control
    uint8_t orientation[] = {0x48};
    LCD_DATA(0x36, orientation);

    // Display inversion off
    LCD_CMD(0x20);

    // Display on
    LCD_CMD(0x29);
    vTaskDelay(pdMS_TO_TICKS(100));

    // ------------------------------------------------
    // ALLOCATE DMA BUFFER
    // ------------------------------------------------

    ESP_LOGI(TAG, "Allocating DMA buffer");

    uint8_t *buffer = heap_caps_malloc(
        BUFFER_SIZE,
        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL
    );

    if (buffer == NULL) {
        ESP_LOGE(TAG, "DMA allocation failed");
        return;
    }

    // ------------------------------------------------
    // COLOR TEST
    // ------------------------------------------------

    const uint16_t test_colors[] = {
        0xF800, // Red
        0x07E0, // Green
        0x001F, // Blue
        0xFFFF, // White
        0x0000, // Black
        0xF81F  // Magenta
    };

    const char *color_names[] = {
        "Red",
        "Green",
        "Blue",
        "White",
        "Black",
        "Magenta"
    };

    for (int color = 0; color < 6; color++) {

        ESP_LOGI(TAG, "Displaying %s", color_names[color]);

        // Fill DMA buffer with RGB565 pixels.
        for (int i = 0; i < BUFFER_SIZE; i += 2) {
            buffer[i] = (test_colors[color] >> 8) & 0xFF;
            buffer[i + 1] = test_colors[color] & 0xFF;
        }

        // Draw the entire display in chunks.
        for (int y = 0; y < LCD_HEIGHT; y += BUFFER_LINES) {

            uint8_t x_addr[] = {
                0x00, 0x00,
                0x01, 0xDF
            };

            uint8_t y_addr[] = {
                (y >> 8) & 0xFF,
                y & 0xFF,

                ((y + BUFFER_LINES - 1) >> 8) & 0xFF,
                (y + BUFFER_LINES - 1) & 0xFF
            };

            // Set column address.
            ESP_ERROR_CHECK(
                esp_lcd_panel_io_tx_param(
                    lcd_io, 0x2A, x_addr, sizeof(x_addr)
                )
            );

            // Set page address.
            ESP_ERROR_CHECK(
                esp_lcd_panel_io_tx_param(
                    lcd_io, 0x2B, y_addr, sizeof(y_addr)
                )
            );

            // Send pixel data.
            ESP_ERROR_CHECK(
                esp_lcd_panel_io_tx_color(
                    lcd_io,
                    0x2C,
                    buffer,
                    BUFFER_SIZE
                )
            );

            // Queue depth is one. The next command is queued
            // after the color transfer, so it acts as a
            // synchronization point before buffer reuse.
        }

        // Wait for the last queued transfer to finish.
        ESP_ERROR_CHECK(
            esp_lcd_panel_io_tx_param(
                lcd_io, 0x00, NULL, 0
            )
        );

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // ------------------------------------------------
    // CLEANUP
    // ------------------------------------------------

    free(buffer);

    ESP_LOGI(TAG, "LCD test finished");

    #undef LCD_CMD
    #undef LCD_DATA
}

void led_stuff()
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_DATA,
        .max_leds = LED_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags.with_dma = false,
    };

    ESP_ERROR_CHECK(
        led_strip_new_rmt_device(
            &strip_config,
            &rmt_config,
            &strip
        )
    );

    size_t c = 0;
    int i = 0;

    while (1)
    {
        while (i < LED_COUNT)
        {
            ESP_ERROR_CHECK(
            led_strip_set_pixel(
                strip,
                i,
                0xff,
                0x00,
                0xff
            )
            );
            i++;
        }

        ESP_ERROR_CHECK(led_strip_refresh(strip));

        vTaskDelay(pdMS_TO_TICKS(1000));

        if (++c >= COLORS_TOTAL)
            c = 0;
    }
}

void led_task(void *pvParameters)
{
    led_stuff();
    vTaskDelete(NULL);
    ESP_LOGI(TAG, "exiting led task");
}

void speaker_Stuff()
{
    ESP_LOGI(TAG, "Initializing speaker");

    i2s_chan_handle_t tx_handle;

    i2s_chan_config_t chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(
            I2S_NUM_1,
            I2S_ROLE_MASTER
        );

    ESP_ERROR_CHECK(
        i2s_new_channel(&chan_cfg, &tx_handle, NULL)
    );

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),

        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT,
            I2S_SLOT_MODE_MONO
        ),

        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCLK,
            .ws = I2S_LRCK,
            .dout = I2S_DIN,
            .din = I2S_GPIO_UNUSED,
        },
    };

    ESP_LOGI(TAG, "checking error");

    ESP_ERROR_CHECK(
        i2s_channel_init_std_mode(tx_handle, &std_cfg)
    );

    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));

    ESP_LOGI(TAG, "Playing 440Hz tone");

    int16_t samples[256];
    float phase = 0.0f;

    while (1)
    {
        
        for (int i = 0; i < 256; i++)
        {
            samples[i] = (int16_t)(
                sinf(phase) * AMPLITUDE
            );

            phase += 2.0f * M_PI * FREQUENCY / SAMPLE_RATE;

            if (phase >= 2.0f * M_PI)
                phase -= 2.0f * M_PI;
        }

        size_t bytes_written = 0;

        ESP_ERROR_CHECK(
            i2s_channel_write(
                tx_handle,
                samples,
                sizeof(samples),
                &bytes_written,
                portMAX_DELAY
            )
        );
    }
}


void speaker_task(void *pvParameters)
{
    speaker_Stuff();

    vTaskDelete(NULL);
    ESP_LOGI(TAG, "exiting speaker task");
}

void app_main(void)
{

    sdmmc_card_t *card;

    esp_err_t ret;

    // SPI bus configuration
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    ret = spi_bus_initialize(
        SPI2_HOST,
        &bus_cfg,
        SPI_DMA_CH_AUTO
    );
    ESP_ERROR_CHECK(ret);

    // SD SPI configuration
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;






    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = SPI2_HOST;

    // Mount filesystem
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    

    ret = esp_vfs_fat_sdspi_mount(
        "/sdcard",
        &host,
        &slot_config,
        &mount_config,
        &card
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD card mount failed: %s",
                 esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "SD card mounted!");

    // Write a file
    FILE *f = fopen("/sdcard/test.txt", "w");

    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file");
        return;
    }

    fprintf(f, "Hello from ESP32!\n");
    fclose(f);

    // Read the file
    char line[128];

    f = fopen("/sdcard/test.txt", "r");

    if (f != NULL) {
        if (fgets(line, sizeof(line), f)) {
            ESP_LOGI(TAG, "Read: %s", line);
        }

        fclose(f);
    }

    // Display card information
    sdmmc_card_print_info(stdout, card);

    ESP_LOGI(TAG, "attempting speakers");

    xTaskCreate(
        speaker_task,
        "speaker_task",
        4096,
        NULL,
        5,
        NULL
    );
    xTaskCreate(
        print_input_task,
        "print_input_task",
        4096,
        NULL,
        5,
        NULL
    );

    lcd_test();

    xTaskCreate(
        dist_task,
        "dist_task",
        4096,
        NULL,
        5,
        NULL
    );

    led_stuff();

}
