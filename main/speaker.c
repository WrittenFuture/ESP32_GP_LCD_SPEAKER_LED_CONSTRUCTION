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
#include "esp_timer.h"

static const char *TAG = "SPEAKER";
static const char *TAG_DECODER = "SPEAKER_DECODER";
static const char *TAG_PLAYER = "SPEAKER_PLAYER";

speaker_config_t *speaker_init(void)
{
    ESP_LOGI(TAG, "Initializing speaker");

    i2s_chan_handle_t tx_handle;

    // configure the I2S channel
    i2s_chan_config_t chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(
            SPK_I2S_NUM,
            I2S_ROLE_MASTER);

    // create the I2S channel and chceck for errors
    ESP_ERROR_CHECK(
        i2s_new_channel(&chan_cfg, &tx_handle, NULL));

    // configure the I2S standard mode
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SPK_SAMPLE_RATE),

        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT,
            I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = SPK_I2S_BCLK,
            .ws = SPK_I2S_LRCK,
            .dout = SPK_I2S_DIN,
            .din = I2S_GPIO_UNUSED,
        },
    };

    ESP_ERROR_CHECK(
        i2s_channel_init_std_mode(tx_handle, &std_cfg));

    ESP_ERROR_CHECK(
        i2s_channel_enable(tx_handle));

    // initialize decoder
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

void disable_i2s(i2s_chan_handle_t tx_handle)
{
    i2s_channel_disable(tx_handle);
}
void enable_i2s(i2s_chan_handle_t tx_handle)
{
    i2s_channel_enable(tx_handle);
}

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
        &frame);

    if (ret != ESP_AUDIO_ERR_OK)
    {
        ESP_LOGE(TAG, "MP3 decoding failed");
        return ESP_FAIL;
    }

    esp_audio_simple_dec_info_t audio_info = {0};

    ret = esp_audio_simple_dec_get_info(decoder, &audio_info);

    if (ret != ESP_AUDIO_ERR_OK)
    {
        ESP_LOGE(TAG, "Failed to get audio information");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Sample rate: %lu", (unsigned long)audio_info.sample_rate);
    ESP_LOGI(TAG, "Bits per sample: %u", audio_info.bits_per_sample);
    ESP_LOGI(TAG, "Channels: %u", audio_info.channel);
    ESP_LOGI(TAG, "Only consumed %d bytes", raw.consumed);

    // move uncconsumed bytes to beginning of buffer
    memmove(
        raw_buffer->data,
        raw_buffer->data + raw.consumed,
        raw_buffer->size - raw.consumed);

    // set unconumed size
    *unconsumed_size = raw_buffer->size - raw.consumed;
    *decoded_size = frame.decoded_size;

    return ESP_OK;
}

esp_err_t play_audio(
    buffer_u8_t *aud_buffer,
    buffer_u8_t *pcm_buffer,
    speaker_config_t *speaker_config,
    size_t *unconsumed_size)
{
    if (aud_buffer == NULL || pcm_buffer == NULL || speaker_config == NULL)
    {
        ESP_LOGE(TAG, "buffers for audio playback decoding are null");
        return ESP_FAIL;
    }

    size_t pcm_len;

    esp_err_t decode_ret = decode(
        pcm_buffer,
        aud_buffer,
        speaker_config->decoder,
        unconsumed_size,
        &pcm_len);
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
        portMAX_DELAY);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "I2S write failed: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "Finished playing frame: %zu", bytes_written);

    return ESP_OK;
}

void stream_decode(void *pvParameters)
{

    ESP_LOGI(TAG_DECODER, "stream_decode running on core %d", xPortGetCoreID());

    stream_decode_params *params = (stream_decode_params *)pvParameters;

    if (params == NULL ||
        params->raw_data_stream == NULL ||
        params->pcm_queue == NULL ||
        params->speaker_config == NULL ||
        params->speaker_config->decoder == NULL)
    {
        ESP_LOGE(TAG_DECODER, "Invalid decoder parameters");
        vTaskDelete(NULL);
        return;
    }

    buffer_u8_t temp_pcm_buff = get_buffer(PCM_BUFFER_SIZE);
    buffer_u8_t temp_raw_buff = get_buffer(AUD_BUFFER_SIZE + AUD_BUFFER_SIZE);

    if (temp_pcm_buff.data == NULL || temp_raw_buff.data == NULL)
    {
        ESP_LOGE(TAG_DECODER, "Failed to allocate decoder buffers");

        free(temp_pcm_buff.data);
        free(temp_raw_buff.data);

        vTaskDelete(NULL);
        return;
    }

    const size_t redecode_threshold = AUD_BUFFER_SIZE / 2;

    size_t tot_unconsumed = 0;
    size_t unconsumed_size = 0;
    size_t decoded_size = 0;

    while (1)
    {

        // Ensure the next 4096-byte item fits.
        if (tot_unconsumed > temp_raw_buff.size - AUD_BUFFER_SIZE)
        {
            ESP_LOGE(TAG_DECODER, "Raw buffer capacity exceeded");
            tot_unconsumed = 0;
        }

        // Receive the next compressed audio chunk after existing data.
        ESP_LOGI(TAG_DECODER, "waiting for raw_data");
        if (xQueueReceive(
                params->raw_data_stream,
                temp_raw_buff.data + tot_unconsumed,
                portMAX_DELAY) != pdPASS)
        {
            ESP_LOGE(TAG_DECODER, "Failed to receive raw data");
            continue;
        }

        tot_unconsumed += AUD_BUFFER_SIZE;

        bool first_pass = true;

        // Always attempt one decode after receiving new data.
        // Continue while enough compressed data remains.
        while (first_pass || tot_unconsumed > redecode_threshold)
        {

            first_pass = false;

            size_t previous_unconsumed = tot_unconsumed;

            buffer_u8_t rebound_pcm_buff = {
                .data = temp_pcm_buff.data,
                .size = temp_pcm_buff.size};

            buffer_u8_t rebound_raw_buff = {
                .data = temp_raw_buff.data,
                .size = tot_unconsumed};

            unconsumed_size = 0;
            decoded_size = 0;

            esp_err_t ret = decode(
                &rebound_pcm_buff,
                &rebound_raw_buff,
                params->speaker_config->decoder,
                &unconsumed_size,
                &decoded_size);

            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG_DECODER, "Decode failed");
                break;
            }

            // Validate the decoder's reported output size.
            if (decoded_size > temp_pcm_buff.size)
            {
                ESP_LOGE(TAG_DECODER,
                         "Decoder output exceeds PCM buffer: %zu",
                         decoded_size);
                break;
            }

            // Update the amount of compressed data remaining.
            if (unconsumed_size > previous_unconsumed)
            {
                ESP_LOGE(TAG_DECODER, "Invalid unconsumed size");
                break;
            }

            tot_unconsumed = unconsumed_size;

            // Only send non-empty PCM frames.
            if (decoded_size > 0)
            {
                buffer_u8_t push_buff = get_buffer(decoded_size);

                if (push_buff.data == NULL)
                {
                    ESP_LOGE(TAG_DECODER, "Failed to allocate PCM output");
                    break;
                }

                push_buff.size = decoded_size;

                memcpy(
                    push_buff.data,
                    temp_pcm_buff.data,
                    decoded_size);

                // Queue copies the descriptor, not the PCM data.
                // Ownership of push_buff.data transfers to the receiver.
                if (xQueueSend(
                        params->pcm_queue,
                        &push_buff,
                        portMAX_DELAY) != pdPASS)
                {
                    ESP_LOGE(TAG_DECODER, "Failed to queue PCM data");
                    free(push_buff.data);
                    break;
                }
            }

            // Prevent an infinite loop if the decoder makes no progress.
            if (tot_unconsumed == previous_unconsumed &&
                decoded_size == 0)
            {
                ESP_LOGW(TAG_DECODER, "Decoder made no progress");
                break;
            }

            if (tot_unconsumed == previous_unconsumed)
            {
                ESP_LOGW(TAG_DECODER, "Decoder did not consume input");
                break;
            }
        }
    }

    free(temp_pcm_buff.data);
    free(temp_raw_buff.data);
    vTaskDelete(NULL);
}

void stream_decode_old(void *pvParameters)
{

    ESP_LOGI(TAG_DECODER, "stream_decode running on core %d", xPortGetCoreID());

    stream_decode_params *params = (stream_decode_params *)pvParameters;

    if (params == NULL ||
        params->raw_data_stream == NULL ||
        params->pcm_queue == NULL ||
        params->speaker_config == NULL ||
        params->speaker_config->decoder == NULL)
    {
        ESP_LOGE(TAG_DECODER, "Invalid decoder parameters");
        vTaskDelete(NULL);
        return;
    }

    buffer_u8_t temp_pcm_buff = get_buffer(PCM_BUFFER_SIZE);
    buffer_u8_t temp_raw_buff = get_buffer(AUD_BUFFER_SIZE + AUD_BUFFER_SIZE);

    if (temp_pcm_buff.data == NULL || temp_raw_buff.data == NULL)
    {
        ESP_LOGE(TAG_DECODER, "Failed to allocate decoder buffers");

        free(temp_pcm_buff.data);
        free(temp_raw_buff.data);

        vTaskDelete(NULL);
        return;
    }

    const size_t redecode_threshold = AUD_BUFFER_SIZE / 2;

    size_t tot_unconsumed = 0;
    size_t unconsumed_size = 0;
    size_t decoded_size = 0;

    while (1)
    {

        // Ensure the next 4096-byte item fits.
        if (tot_unconsumed > temp_raw_buff.size - AUD_BUFFER_SIZE)
        {
            ESP_LOGE(TAG_DECODER, "Raw buffer capacity exceeded");
            tot_unconsumed = 0;
        }

        // Receive the next compressed audio chunk after existing data.
        ESP_LOGI(TAG_DECODER, "waiting for raw_data");
        if (xQueueReceive(
                params->raw_data_stream,
                temp_raw_buff.data + tot_unconsumed,
                portMAX_DELAY) != pdPASS)
        {
            ESP_LOGE(TAG_DECODER, "Failed to receive raw data");
            continue;
        }
        ESP_LOGI(TAG_DECODER, "finished waiting for raw_data");

        tot_unconsumed += AUD_BUFFER_SIZE;

        ESP_LOGI(TAG_DECODER, "Received raw data: %zu bytes total to decode", tot_unconsumed);

        bool first_pass = true;
        int pass = 0;

        // Always attempt one decode after receiving new data.
        // Continue while enough compressed data remains.
        while (first_pass || tot_unconsumed > redecode_threshold)
        {
            ESP_LOGI(TAG_DECODER, "Decoder pass: %d ", pass);
            ESP_LOGI(TAG_DECODER, "unconsumed: %zu ", tot_unconsumed);
            pass++;

            first_pass = false;

            size_t previous_unconsumed = tot_unconsumed;

            buffer_u8_t rebound_pcm_buff = {
                .data = temp_pcm_buff.data,
                .size = temp_pcm_buff.size};

            buffer_u8_t rebound_raw_buff = {
                .data = temp_raw_buff.data,
                .size = tot_unconsumed};

            unconsumed_size = 0;
            decoded_size = 0;

            esp_err_t ret = decode(
                &rebound_pcm_buff,
                &rebound_raw_buff,
                params->speaker_config->decoder,
                &unconsumed_size,
                &decoded_size);

            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG_DECODER, "Decode failed");
                break;
            }

            // Validate the decoder's reported output size.
            if (decoded_size > temp_pcm_buff.size)
            {
                ESP_LOGE(TAG_DECODER,
                         "Decoder output exceeds PCM buffer: %zu",
                         decoded_size);
                break;
            }

            // Update the amount of compressed data remaining.
            if (unconsumed_size > previous_unconsumed)
            {
                ESP_LOGE(TAG_DECODER, "Invalid unconsumed size");
                break;
            }

            tot_unconsumed = unconsumed_size;

            ESP_LOGI(TAG_DECODER,
                     "Decoded: %zu bytes, remaining raw: %zu bytes",
                     decoded_size,
                     tot_unconsumed);

            // Only send non-empty PCM frames.
            if (decoded_size > 0)
            {
                buffer_u8_t push_buff = get_buffer(decoded_size);

                if (push_buff.data == NULL)
                {
                    ESP_LOGE(TAG_DECODER, "Failed to allocate PCM output");
                    break;
                }

                push_buff.size = decoded_size;
                ESP_LOGI(TAG_DECODER, "buffer size: %zu", push_buff.size);

                memcpy(
                    push_buff.data,
                    temp_pcm_buff.data,
                    decoded_size);

                // Queue copies the descriptor, not the PCM data.
                // Ownership of push_buff.data transfers to the receiver.
                if (xQueueSend(
                        params->pcm_queue,
                        &push_buff,
                        portMAX_DELAY) != pdPASS)
                {
                    ESP_LOGE(TAG_DECODER, "Failed to queue PCM data");
                    free(push_buff.data);
                    break;
                }
            }

            // Prevent an infinite loop if the decoder makes no progress.
            if (tot_unconsumed == previous_unconsumed &&
                decoded_size == 0)
            {
                ESP_LOGW(TAG_DECODER, "Decoder made no progress");
                break;
            }

            if (tot_unconsumed == previous_unconsumed)
            {
                ESP_LOGW(TAG_DECODER, "Decoder did not consume input");
                break;
            }
        }
    }

    free(temp_pcm_buff.data);
    free(temp_raw_buff.data);
    vTaskDelete(NULL);
}


void play_pcm_stream(void *pvParameters)
{
    play_pcm_stream_params *params = (play_pcm_stream_params *)pvParameters;
    if (params == NULL)
    {
        vTaskDelete(NULL);
    }

    buffer_u8_t temp_pcm_buffer;

    size_t bytes_written = 0;

    while (1)
    {
        if (xQueueReceive(params->pcm_queue, &temp_pcm_buffer, portMAX_DELAY) == pdPASS)
        {
            esp_err_t ret = i2s_channel_write(
                params->speaker_config->tx_handle,
                temp_pcm_buffer.data,
                temp_pcm_buffer.size,
                &bytes_written,
                portMAX_DELAY);
            free(temp_pcm_buffer.data);
        }
    }
}

void play_pcm_stream_old(void *pvParameters)
{
    play_pcm_stream_params *params = (play_pcm_stream_params *)pvParameters;
    if (params == NULL)
    {
        ESP_LOGE(TAG_PLAYER, "Error getting params");
        vTaskDelete(NULL);
    }

    buffer_u8_t temp_pcm_buffer;

    size_t bytes_written = 0;

    while (1)
    {
        ESP_LOGI(TAG_PLAYER, "Reciving from pcm buffer");
        int64_t wait_start = esp_timer_get_time();
        if (xQueueReceive(params->pcm_queue, &temp_pcm_buffer, portMAX_DELAY) == pdPASS)
        {
            int64_t wait_time = esp_timer_get_time() - wait_start;

            if (wait_time > 1000)
            {
                ESP_LOGW(TAG_PLAYER,
                         "PCM queue starvation: waited %lld ms",
                         wait_time / 1000);
            }
            else
            {
                ESP_LOGI(TAG_PLAYER,
                         "PCM queue: waited %lld ms",
                         wait_time / 1000);
            }

            ESP_LOGI(TAG_PLAYER, "finished reciving from pcm buffer");

            if (temp_pcm_buffer.data == NULL)
            {
                ESP_LOGE(TAG_PLAYER, "Error getting pcm data");
                vTaskDelete(NULL);
            }

            ESP_LOGI(TAG_PLAYER, "Size grabbed: %zu", temp_pcm_buffer.size);

            ESP_LOGI(TAG_PLAYER, "playing sound");
            wait_start = esp_timer_get_time();
            esp_err_t ret = i2s_channel_write(
                params->speaker_config->tx_handle,
                temp_pcm_buffer.data,
                temp_pcm_buffer.size,
                &bytes_written,
                portMAX_DELAY);
            wait_time = esp_timer_get_time() - wait_start;

            if (wait_time > 1000)
            {
                ESP_LOGW(TAG_PLAYER,
                         "to write to i2s: waited %lld ms",
                         wait_time / 1000);
            }
            else
            {
                ESP_LOGI(TAG_PLAYER,
                         "to write to i2s: waited %lld ms",
                         wait_time / 1000);
            }

            free(temp_pcm_buffer.data);

            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG_PLAYER, "I2S write failed: %s", esp_err_to_name(ret));
            }
        }
    }
}