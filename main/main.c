#include <math.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_random.h"

#include "sd_card.h"
#include "speaker.h"
#include "util.h"

static const char *TAG = "MAIN";

void playfile(
    speaker_config_t *speaker_config,
    TaskHandle_t *raw_data_task,
    TaskHandle_t *data_decode_task,
    TaskHandle_t *play_task,
    stream_data_params *raw_data_params,
    stream_decode_params *decode_params,
    play_pcm_stream_params *player_params,
    int *queue_size)
{

    // create the raw data stream
    ESP_LOGI(TAG, "attempting to start raw_data_stream");
    int kret = xTaskCreate(
        stream_data,
        "RawDataStream",
        8192,
        raw_data_params,
        3,
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
    kret = xTaskCreate(
        stream_decode,
        "DecodeDataStream",
        4096,
        decode_params,
        6,
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

    UBaseType_t queued = uxQueueMessagesWaiting(player_params->pcm_queue);
    while (queued != *queue_size)
    {
        ESP_LOGI(TAG, "waiting for queue to fill: %d", queued);
        vTaskDelay(25);
        queued = uxQueueMessagesWaiting(player_params->pcm_queue);
    }
    // start player task
    ESP_LOGI(TAG, "attempting to start player task");
    kret = xTaskCreate(
        play_pcm_stream,
        "PCMplayer",
        8192,
        player_params,
        4,
        play_task);
    if (kret != 1)
    {
        ESP_LOGE(TAG, "an error occured while starting async task play_pcm_stream. return: %d", kret);
        return;
    }
    else
    {
        ESP_LOGI(TAG, "sucsessfully started play_pcm_stream");
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

    // handles to audio tasks
    TaskHandle_t raw_data_task = NULL;
    TaskHandle_t data_decode_task = NULL;
    TaskHandle_t play_task = NULL;

    int music_count = (esp_random() % 86) + 1;
    char file_path[64];

    snprintf(
        file_path, sizeof(file_path),
        MP3_FILE_PATH "%d.mp3", music_count);

    int queue_size = 32;
    // create the task struct
    QueueHandle_t pcm_queue = xQueueCreate(32, sizeof(buffer_u8_t));
    QueueHandle_t raw_data_queue = xQueueCreate(4, AUD_BUFFER_SIZE);

    if (pcm_queue == NULL || raw_data_queue == NULL)
    {
        ESP_LOGE(TAG, "Failed to create audio queues");
        return;
    }

    bool eof = false;

    stream_data_params raw_data_params = {
        .raw_data_queue = raw_data_queue,
        .copy_size = AUD_BUFFER_SIZE,
        .file_path = file_path,
        .eof = &eof};

    stream_decode_params decode_params =
        {
            .pcm_queue = pcm_queue,
            .raw_data_stream = raw_data_queue,
            .speaker_config = speaker_config};

    play_pcm_stream_params player_params =
        {
            .pcm_queue = pcm_queue,
            .speaker_config = speaker_config};

    playfile(
        speaker_config,
        &raw_data_task,
        &data_decode_task,
        &play_task,
        &raw_data_params,
        &decode_params,
        &player_params,
        &queue_size);

    UBaseType_t queued = uxQueueMessagesWaiting(pcm_queue);

    // this shouold stop if bot EOF is true and pcm buffer is empty
    while (!eof || queued != 0)
    {
        ESP_LOGI(TAG, "queue has %d/%d items filled", queued, queue_size);
        vTaskDelay(25);
        queued = uxQueueMessagesWaiting(pcm_queue);
    }

    disable_i2s(speaker_config->tx_handle);
    ESP_LOGI(TAG, "deleting decoder task");
    if (eTaskGetState(data_decode_task) != eDeleted)
    {
        vTaskDelete(data_decode_task);
    }

    ESP_LOGI(TAG, "deleting play");
    if (eTaskGetState(play_task) != eDeleted)
    {
        vTaskDelete(play_task);
    }
    return;
}