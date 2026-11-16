#include <math.h>

#include "esp_err.h"
#include "esp_log.h"

#include "sd_card.h"
#include "speaker.h"

#define AUD_BUFFER_SIZE 4096

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

    //read a mp3 buffer
    uint8_t *buffer = get_buffer(0, AUD_BUFFER_SIZE, MP3_FILE_PATH "59. Minuet of Forest.mp3");

    ESP_LOGI(TAG, "Buffer size: %d", AUD_BUFFER_SIZE);

    if (buffer == NULL) {
        ESP_LOGE(TAG, "Failed to read mp3 file into buffer");
        return;
    }
    ESP_LOGI(TAG, "Successfully read mp3 file into buffer as \n");

    // print the bytes of the buffer in hex
    for (int i = 0; i < log2(AUD_BUFFER_SIZE); i++)
    {
        printf("%02X ", buffer[i]);
        if ((i + 1) % 16 == 0)
        {
            printf("\n");
        }
    }

    printf("\n");


    //initialize the speaker
    speaker_config_t *speaker_config = speaker_init();

    // use the play_audio function to play the audio from the buffer
    play_audio(buffer, AUD_BUFFER_SIZE, speaker_config);


    
}


