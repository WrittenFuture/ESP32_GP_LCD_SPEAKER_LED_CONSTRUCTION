#include "audio_decoder.h"

#include "esp_audio_dec_default.h"
#include "esp_audio_simple_dec_default.h"
#include "esp_log.h"

#define TAG "MP3_DECODER"

esp_audio_err_t mp3_decoder_init(
    esp_audio_simple_dec_handle_t *decoder
)
{
    ESP_LOGI(TAG, "Initializing MP3 decoder");
    // Register the actual audio decoders
    esp_audio_err_t ret = esp_audio_dec_register_default();

    if (ret != ESP_AUDIO_ERR_OK)
        return ret;

    // Register the simple decoder interfaces
    ret = esp_audio_simple_dec_register_default();

    if (ret != ESP_AUDIO_ERR_OK)
        return ret;

    // Configure MP3 decoding
    esp_audio_simple_dec_cfg_t config = {
        .dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3,
        .dec_cfg = NULL,
        .cfg_size = 0,
        .use_frame_dec = false
    };

    // Create the decoder
    ret = esp_audio_simple_dec_open(&config, decoder);

    if (ret != ESP_AUDIO_ERR_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize MP3 decoder: %d", ret);
        return ret;
    }

    ESP_LOGI(TAG, "MP3 decoder initialized");

    return ESP_AUDIO_ERR_OK;
}