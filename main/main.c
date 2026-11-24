#include <math.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_log.h"

#include "sd_card.h"
#include "speaker.h"
#include "util.h"

static const char *TAG = "MAIN";

void playfile(const char *file_path,
              speaker_config_t *speaker_config,
              TaskHandle_t *raw_data_task,
              TaskHandle_t *data_decode_task)
{
    QueueHandle_t pcm_queue = xQueueCreate(4, sizeof(buffer_u8_t));
    QueueHandle_t raw_data_queue = xQueueCreate(4, AUD_BUFFER_SIZE);

    ESP_LOGI(TAG, "raw_data_queue: %p", (void *)raw_data_queue);

    // create the raw data stream
    stream_data_params raw_data_params = {
        .raw_data_queue = raw_data_queue,
        .copy_size = AUD_BUFFER_SIZE,
        .file_path = file_path};

    ESP_LOGI(TAG, "attempting to start raw_data_stream");
    int kret = xTaskCreate(
        stream_data,
        "RawDataStream",
        8192,
        &raw_data_params,
        5,
        raw_data_task);

    if (kret != 1)
    {
        ESP_LOGE(TAG, "an error occured while starting async task raw_data_stream. return: %d", kret);
        return;
    }
    else
    {
        ESP_LOGI(TAG, "sucsessfully started raw_data_stream");
    }

    // create the decoder task
    ESP_LOGI(TAG, "attempting to start data_decode_stream");
    stream_decode_params decode_params =
        {
            .pcm_queue = pcm_queue,
            .raw_data_stream = raw_data_queue,
            .speaker_config = speaker_config};

    kret = xTaskCreate(
        stream_decode,
        "DecodeDataStream",
        4096,
        &decode_params,
        5,
        data_decode_task

    );
    if (kret != 1)
    {
        ESP_LOGE(TAG, "an error occured while starting async task data_decode_stream. return: %d", kret);
        return;
    }
    else
    {
        ESP_LOGI(TAG, "sucsessfully started data_decode_stream");
    }

    // start player task
    play_pcm_stream_params player_params =
        {
            .pcm_queue = pcm_queue,
            .speaker_config = speaker_config};

    ESP_LOGI(TAG, "attempting to start player task");
    kret = xTaskCreate(
        play_pcm_stream,
        "PCMplayer",
        8192,
        &player_params,
        5,
        NULL);
    if (kret != 1)
    {
        ESP_LOGE(TAG, "an error occured while starting async task play_pcm_stream. return: %d", kret);
        return;
    }
    else
    {
        ESP_LOGI(TAG, "sucsessfully started play_pcm_stream");
    }

    while (1)
    {
        vTaskDelay(1000);
    }
}

void app_main(void)
{

    ESP_LOGI(TAG, "main thread running on core %d", xPortGetCoreID());

    speaker_config_t *speaker_config = speaker_init();
    if (speaker_config == NULL)
    {
        ESP_LOGE(TAG, "Failed to initialize speaker");
        return;
    }

    sd_card_init(MOUNT_POINT1);

    TaskHandle_t raw_data_task = NULL;
    TaskHandle_t data_decode_task = NULL;
    // TaskHandle_t *play_task;

    playfile(MP3_FILE_PATH "59. Minuet of Forest.mp3",
             speaker_config,
             &raw_data_task,
             &data_decode_task);
}
/*
void app_main(void)
{

    // initialize the SD card
    esp_err_t ret = sd_card_init(MOUNT_POINT1);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize SD card: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "SD card initialized successfully");

    // initialize the speaker
    speaker_config_t *speaker_config = speaker_init();
    if (speaker_config == NULL)
    {
        ESP_LOGE(TAG, "Failed to initialize speaker");
        return;
    }

    // get an audio buffer
    buffer_u8_t aud_buffer = get_buffer(AUD_BUFFER_SIZE);
    if (aud_buffer.data == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate audio buffer");
        return;
    }

    // get a pcm buffer
    buffer_u8_t pcm_buffer = get_buffer(PCM_BUFFER_SIZE);
    if (pcm_buffer.data == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate PCM buffer");
        free(aud_buffer.data);
        return;
    }

    size_t unconsumed_size = 0;
    size_t total_read = 0;
    int counter = 0;

    while (1)
    {
        // fill the buffer with raw data
        ret = copy_data(&aud_buffer, unconsumed_size, total_read, AUD_BUFFER_SIZE - unconsumed_size, MP3_FILE_PATH "59. Minuet of Forest.mp3");
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to copy data");
            continue;
        }
        total_read += AUD_BUFFER_SIZE - unconsumed_size;

        ret = play_audio(&aud_buffer, &pcm_buffer, speaker_config, &unconsumed_size);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to play audio");
            continue;
        }

        counter++;

        printf("\n");
        ESP_LOGI(TAG, "buffer read #%d", counter);
    }

    free(aud_buffer.data);
    free(pcm_buffer.data);
}
*/
