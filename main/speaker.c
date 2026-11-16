#include <stdio.h>
#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "speaker.h"

#include "driver/i2s_std.h"
#include "esp_err.h"
#include "esp_log.h"

#include "esp_audio_dec_default.h"
#include "esp_audio_simple_dec_default.h"

#include "audio_decoder.h"

static const char *TAG = "SPEAKER";

typedef struct 
{
    uint8_t *pcm_buffer;
    uint32_t pcm_buffer_size;

} frame_t;

speaker_config_t *speaker_init(void)
{
    ESP_LOGI(TAG, "Initializing speaker");
    
    i2s_chan_handle_t tx_handle;

    // configure the I2S channel
    i2s_chan_config_t chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(
            SPK_I2S_NUM,
            I2S_ROLE_MASTER
        );
    
    // create the I2S channel and chceck for errors
    ESP_ERROR_CHECK(
        i2s_new_channel(&chan_cfg, &tx_handle, NULL)
    );

    // configure the I2S standard mode
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SPK_SAMPLE_RATE),

        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT,
            I2S_SLOT_MODE_STEREO
        ),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = SPK_I2S_BCLK,
            .ws = SPK_I2S_LRCK,
            .dout = SPK_I2S_DIN,
            .din = I2S_GPIO_UNUSED,
        },
    };

    ESP_ERROR_CHECK(
        i2s_channel_init_std_mode(tx_handle, &std_cfg)
    );

    ESP_ERROR_CHECK(
        i2s_channel_enable(tx_handle)
    );

    speaker_config_t *config = malloc(sizeof(speaker_config_t));
    config->tx_handle = tx_handle;
    config->std_cfg = std_cfg;

    return config;
}

frame_t *get_frame(uint8_t *audio_buffer, int buffer_size)
{

    frame_t *new_frame = malloc(sizeof(frame_t));
    if (new_frame == NULL) {
        ESP_LOGE(TAG, "Failed to allocate frame");
        return NULL;
    }

    esp_audio_simple_dec_handle_t decoder = NULL;

    esp_err_t ret = mp3_decoder_init(&decoder);

    if (ret != ESP_AUDIO_ERR_OK)
    {
        ESP_LOGE(TAG, "MP3 initialization failed");
        return NULL;
    }

    printf("\n");


    esp_audio_simple_dec_raw_t raw = {
        .buffer = audio_buffer,
        .len = buffer_size,
        .eos = false,
    };

    // Allocate a buffer for PCM data, size is arbitrary
    uint8_t *pcm_buffer = malloc(buffer_size * 4); 
        if (pcm_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate PCM buffer");
        esp_audio_simple_dec_close(decoder);
        free(new_frame);
        return NULL;
    }

    uint32_t pcm_buffer_size = buffer_size * 4; // Adjust size as needed

    esp_audio_simple_dec_out_t frame = {
        .buffer = pcm_buffer,
        .len = pcm_buffer_size,
    };

    ret = esp_audio_simple_dec_process(
        decoder,
        &raw,
        &frame
    );

    if (ret != ESP_AUDIO_ERR_OK)
    {
        ESP_LOGE(TAG, "MP3 decoding failed");
        free(pcm_buffer);
        esp_audio_simple_dec_close(decoder);
        free(new_frame);
        return NULL;
    }

    esp_audio_simple_dec_info_t audio_info = {0};

    ret = esp_audio_simple_dec_get_info(decoder, &audio_info);

    if (ret != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(TAG, "Failed to get audio information");
        free(pcm_buffer);
        esp_audio_simple_dec_close(decoder);
        free(new_frame);
        return NULL;
    }

    ESP_LOGI(TAG, "Sample rate: %lu", (unsigned long)audio_info.sample_rate);
    ESP_LOGI(TAG, "Bits per sample: %u", audio_info.bits_per_sample);
    ESP_LOGI(TAG, "Channels: %u", audio_info.channel);

    new_frame->pcm_buffer = pcm_buffer;
    new_frame->pcm_buffer_size = frame.decoded_size;
    

    return new_frame;

}

void play_audio(uint8_t *audio_buffer, int buffer_size, speaker_config_t *speaker_config)
{

    frame_t *frame = get_frame(audio_buffer, buffer_size);
    if (frame == NULL) {
        ESP_LOGE(TAG, "Failed to allocate frame");
        return;
    }
    
    size_t bytes_written = 0;

    ESP_LOGI(TAG, "playing one frame");

    esp_err_t ret = i2s_channel_write(
        speaker_config->tx_handle,
        frame->pcm_buffer,
        frame->pcm_buffer_size,
        &bytes_written,
        portMAX_DELAY
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S write failed: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "Finished playing frame: %zu", bytes_written);

    free(frame->pcm_buffer);
    free(frame);

}