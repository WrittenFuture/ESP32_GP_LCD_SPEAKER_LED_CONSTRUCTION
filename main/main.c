#include <math.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_log.h"

#include "sd_card.h"
#include "speaker.h"
#include "util.h"

#define AUD_BUFFER_SIZE 4096
#define PCM_BUFFER_SIZE 8192

static const char *TAG = "MAIN";

void app_main(void)
{

    // initialize the SD card 
    esp_err_t ret = sd_card_init(MOUNT_POINT1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SD card: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "SD card initialized successfully");

    //initialize the speaker
    speaker_config_t *speaker_config = speaker_init();
    if (speaker_config == NULL) {
        ESP_LOGE(TAG, "Failed to initialize speaker");
        return;
    }

    // get an audio buffer
    buffer_u8_t aud_buffer = get_buffer(AUD_BUFFER_SIZE);
    if (aud_buffer.data == NULL) {
        ESP_LOGE(TAG, "Failed to allocate audio buffer");
        return;
    }

    // get a pcm buffer
    buffer_u8_t pcm_buffer = get_buffer(PCM_BUFFER_SIZE);
    if (pcm_buffer.data == NULL) {
        ESP_LOGE(TAG, "Failed to allocate PCM buffer");
        free(aud_buffer.data);
        return;
    }

    size_t unconsumed_size = 0;
    size_t total_read = 0;
    int counter = 0;

    while(1)
    {
        // fill the buffer with raw data
        ret = copy_data(&aud_buffer, unconsumed_size, total_read, AUD_BUFFER_SIZE - unconsumed_size, MP3_FILE_PATH "59. Minuet of Forest.mp3");
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to copy data");
            continue;
        }
        total_read += AUD_BUFFER_SIZE - unconsumed_size;

        ret = play_audio(&aud_buffer, &pcm_buffer, speaker_config, &unconsumed_size);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to play audio");
            continue;
        }

        counter ++;

        printf("\n");
        ESP_LOGI(TAG, "buffer read #%d", counter);
    }

    free(aud_buffer.data);
    free(pcm_buffer.data);
}

