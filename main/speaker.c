#include <stdio.h>
#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "speaker.h"
#include "util.h"

#include "driver/i2s_std.h"
#include "esp_err.h"
#include "esp_log.h"

#include "esp_audio_dec_default.h"
#include "esp_audio_simple_dec_default.h"

#include "audio_decoder.h"

static const char *TAG = "SPEAKER";

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

    //initialize decoder
    esp_audio_simple_dec_handle_t decoder = NULL;

    esp_err_t ret = mp3_decoder_init(&decoder);

    if (ret != ESP_AUDIO_ERR_OK)
    {
        ESP_LOGE(TAG, "MP3 initialization failed");
        esp_audio_simple_dec_close(decoder);
        return NULL;
    }

    printf("\n");

    speaker_config_t *config = malloc(sizeof(speaker_config_t));
    if (config == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate speaker configuration");
        esp_audio_simple_dec_close(decoder);
        i2s_channel_disable(tx_handle);
        i2s_del_channel(tx_handle);
        return NULL;
    }
    config->tx_handle = tx_handle;
    config->std_cfg = std_cfg;
    config->decoder = decoder;

    return config;
}

/*
frame_t *get_frame_old(
    uint8_t *audio_buffer, 
    uint8_t *unconsumed_bytes, 
    size_t unconsumed_size, 
    size_t buffer_size, 
    esp_audio_simple_dec_handle_t decoder)
{

    // return value is freed by caller
    frame_t *new_frame = malloc(sizeof(frame_t));
    if (new_frame == NULL) {
        ESP_LOGE(TAG, "Failed to allocate frame");
        return NULL;
    }

    //handel leftover data from last frame if present

    //allocate data for both pointers size
    size_t raw_data_size = buffer_size + unconsumed_size;
    uint8_t *raw_data = malloc(raw_data_size);

    if (raw_data == NULL)
    {
        ESP_LOGE(TAG, "failed to allowcate raw data buffer");
        free(new_frame);
        return NULL;
    }

    if (unconsumed_bytes != NULL)
    {
        //copy data from both buffers into raw data to pass to the function
        memcpy(raw_data, unconsumed_bytes, unconsumed_size);
        memcpy(raw_data + unconsumed_size, audio_buffer, buffer_size);
    }
    else
    {
        //if unconsumed is null there is no leftover data to handle
        raw_data_size = buffer_size;
        memcpy(raw_data, audio_buffer, buffer_size);
    }

    // process raw audio
    esp_audio_simple_dec_raw_t raw = {
        .buffer = raw_data,
        .len = raw_data_size,
        .eos = false,
    };

    // Allocate a buffer for PCM data, size is arbitrary
    size_t pcm_capacity = 8192;

    uint8_t *pcm_buffer = malloc(pcm_capacity);
    if (pcm_buffer == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate pcm_buffer");
        ESP_LOGW(TAG, "needed %zu bytes in memory had %u with the largest block of %u bytes", 
            pcm_capacity,
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT), 
            (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        free(new_frame);
        return NULL;
    }

    esp_audio_simple_dec_out_t frame = {
        .buffer = pcm_buffer,
        .len = pcm_capacity,
    };

    esp_err_t ret = esp_audio_simple_dec_process(
        decoder,
        &raw,
        &frame
    );

    if (ret != ESP_AUDIO_ERR_OK)
    {
        ESP_LOGE(TAG, "MP3 decoding failed");
        free(pcm_buffer);
        free(new_frame);
        free(raw_data);
        return NULL;
    }

    esp_audio_simple_dec_info_t audio_info = {0};

    ret = esp_audio_simple_dec_get_info(decoder, &audio_info);

    if (ret != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(TAG, "Failed to get audio information");
        free(pcm_buffer);
        free(raw_data);
        free(new_frame);
        return NULL;
    }

    ESP_LOGI(TAG, "Sample rate: %lu", (unsigned long)audio_info.sample_rate);
    ESP_LOGI(TAG, "Bits per sample: %u", audio_info.bits_per_sample);
    ESP_LOGI(TAG, "Channels: %u", audio_info.channel);
    ESP_LOGI(TAG, "Only consumed %d bytes out of %d",raw.consumed , raw_data_size);

    // calculate size of unconsumed_bytes
    size_t new_unconsumed_size = raw_data_size - raw.consumed;

    // copy the unconsumed bytes into the unconsumed_bytes buffer for the next frame
    ESP_LOGI(TAG, "Allocating %zu bytes to unconsumed buffer", new_unconsumed_size);

    //reallocate unconsumed to the correct size for return value
    uint8_t *new_unconsumed_bytes = malloc(new_unconsumed_size);

    // copy unconsumed bytes into unconsumed buffer
    if (new_unconsumed_bytes != NULL)
    {
        memcpy(
            new_unconsumed_bytes,
            raw_data + raw.consumed,
            new_unconsumed_size
        );
    }
    else
    {
        ESP_LOGE(TAG, "could not copy into unconsumed bytes buffer");
        free(pcm_buffer);
        free(raw_data);
        free(new_frame);
        return NULL;
    }

    new_frame->pcm_buffer = pcm_buffer;
    new_frame->pcm_buffer_size = frame.decoded_size;
    new_frame->unconsumed_bytes = new_unconsumed_bytes; 
    new_frame->unconsumed_bytes_size = new_unconsumed_size;

    //raw data isnt needed anymore
    free(raw_data);

    return new_frame;

}
*/
esp_err_t decode(buffer_u8_t *pcm_buffer, 
    buffer_u8_t *raw_buffer, 
    esp_audio_simple_dec_handle_t decoder,
    size_t *unconsumed_size,
    size_t *decoded_size)
{

    if (pcm_buffer == NULL || raw_buffer == NULL ||
        pcm_buffer->data == NULL || raw_buffer->data == NULL ||
        unconsumed_size == NULL || decoded_size == NULL)
    {
        ESP_LOGE(TAG, "buffers for mp3 decoding are null");
        return ESP_ERR_INVALID_ARG;
    }

    esp_audio_simple_dec_raw_t raw = {
        .buffer = raw_buffer->data,
        .len = raw_buffer->size,
        .eos = false,
    };

    esp_audio_simple_dec_out_t frame = {
        .buffer = pcm_buffer->data,
        .len = pcm_buffer->size,
    };

    esp_err_t ret = esp_audio_simple_dec_process(
        decoder,
        &raw,
        &frame
    );

    if (ret != ESP_AUDIO_ERR_OK)
    {
        ESP_LOGE(TAG, "MP3 decoding failed");
        return ESP_FAIL;
    }

    esp_audio_simple_dec_info_t audio_info = {0};

    ret = esp_audio_simple_dec_get_info(decoder, &audio_info);

    if (ret != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(TAG, "Failed to get audio information");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Sample rate: %lu", (unsigned long)audio_info.sample_rate);
    ESP_LOGI(TAG, "Bits per sample: %u", audio_info.bits_per_sample);
    ESP_LOGI(TAG, "Channels: %u", audio_info.channel);
    ESP_LOGI(TAG, "Only consumed %d bytes",raw.consumed);

    //move uncconsumed bytes to beginning of buffer
    memcpy(
        raw_buffer->data,
        raw_buffer->data + raw.consumed,
        raw_buffer->size - raw.consumed
    );
    
    //set unconumed size
    *unconsumed_size = raw_buffer->size - raw.consumed;
    *decoded_size = frame.decoded_size;

    return ESP_OK;

}

esp_err_t play_audio(
    buffer_u8_t *aud_buffer, 
    buffer_u8_t *pcm_buffer,
    speaker_config_t *speaker_config, 
    size_t *unconsumed_size
)
{
    if (aud_buffer == NULL || pcm_buffer == NULL || speaker_config == NULL)
    {
        ESP_LOGE(TAG, "buffers for audio playbacl decoding are null");
        return ESP_FAIL;
    }

    size_t pcm_len;

    esp_err_t decode_ret = decode(
        pcm_buffer,
        aud_buffer,
        speaker_config->decoder,
        unconsumed_size,
        &pcm_len
    );
    if (decode_ret != ESP_OK)
    {
        return decode_ret;
    }

    size_t bytes_written = 0;

    esp_err_t ret = i2s_channel_write(
        speaker_config->tx_handle,
        pcm_buffer->data,
        pcm_len,
        &bytes_written,
        portMAX_DELAY
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S write failed: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "Finished playing frame: %zu", bytes_written);

    return ESP_OK;
}

/*
{

    frame_t *frame = NULL;


    if (prev_frame == NULL)
    {
        frame = get_frame(audio_buffer, NULL, sizeof(NULL), buffer_size, speaker_config->decoder);
        if (frame == NULL) {
            ESP_LOGE(TAG, "Failed to allocate frame");
            free(frame);
            return NULL;
        }
        free(prev_frame);
    }
    else
    {
        frame = get_frame(audio_buffer, prev_frame->unconsumed_bytes, 
                        prev_frame->unconsumed_bytes_size, 
                        buffer_size, speaker_config->decoder);
        if (frame == NULL) {
            ESP_LOGE(TAG, "Failed to retrive frame");
            free(prev_frame->pcm_buffer);
            free(prev_frame->unconsumed_bytes);
            free(prev_frame);
            return NULL;
        }
    }

    if (frame == NULL)
    {
        ESP_LOGE(TAG, "Failed to generate audio frame");
        return NULL;
    }
    
    size_t bytes_written = 0;

    ESP_LOGI(TAG, "playing one frame");

    size_t replay_threshold = 1024;

    // re attempt decoding if unconsumed is too large
    while (frame->unconsumed_bytes_size > replay_threshold)
    {
        ESP_LOGI(TAG, "caling intermediate frame");

        frame_t *intermetent_frame = NULL;

        if (frame->unconsumed_bytes != NULL)
        {
            // use unconsumed as raw for deecoding
            intermetent_frame = get_frame(frame->unconsumed_bytes, NULL, 0, frame->unconsumed_bytes_size, speaker_config->decoder);
            if (intermetent_frame == NULL)
            {
                ESP_LOGI(TAG, "intermentant frame failed");
                return NULL;
            }
        }
        else
        {
            ESP_LOGE(TAG, "unconsumed bytes for decoding was null");
            break;
        }

        //combine pcm buffer and buffer sizes 
        size_t old_pcm_size = frame->pcm_buffer_size;
        size_t new_pcm_size = frame->pcm_buffer_size + intermetent_frame->pcm_buffer_size;
        uint8_t *temp = realloc(frame->pcm_buffer, new_pcm_size);

        if (temp == NULL)
        {
            ESP_LOGE(TAG, "Failed to reallocate PCM buffer");
            ESP_LOGW(TAG, "needed %zu bytes in memory had %u with the largest block of %u bytes", 
                new_pcm_size,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT), 
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
            // frame->pcm_buffer is still valid here
            return NULL;
        }

        frame->pcm_buffer = temp;
        frame->pcm_buffer_size = new_pcm_size;

        
        memcpy(
            frame->pcm_buffer + old_pcm_size,
            intermetent_frame->pcm_buffer,
            intermetent_frame->pcm_buffer_size
        );


        ESP_LOGI(TAG, "pcm_buffer: %p", (void *)frame->pcm_buffer);
        ESP_LOGI(TAG, "old PCM size: %zu", old_pcm_size);
        ESP_LOGI(TAG, "pintermetent frame pcm buffer: %p", (void *)intermetent_frame->pcm_buffer);
        ESP_LOGI(TAG, "intermentent frame PCM buffer size: %zu", intermetent_frame->pcm_buffer_size);

        if (frame->pcm_buffer == NULL)
        {
            ESP_LOGE(TAG, " AFTER REALOC Intermetent frame -> PCM_BUFFER failed is null and beign skipped");
            ESP_LOGW(TAG, "needed %zu memory and had %u with the smallest block being %lu", 
                frame->pcm_buffer_size, 
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT), 
                (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
            free(intermetent_frame);
            free(frame);
            return NULL;
        }

        memcpy(frame->pcm_buffer + old_pcm_size, intermetent_frame->pcm_buffer, intermetent_frame->pcm_buffer_size);

        //update frame with the new unconsumed
        free(frame->unconsumed_bytes);

        frame->unconsumed_bytes = intermetent_frame->unconsumed_bytes;
        frame->unconsumed_bytes_size = intermetent_frame->unconsumed_bytes_size;

        intermetent_frame->unconsumed_bytes = NULL;

        //intermentent frame is no longer needed
        free(intermetent_frame->pcm_buffer);
        free(intermetent_frame->unconsumed_bytes);
        free(intermetent_frame);
    }

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

    return frame;

}
    */