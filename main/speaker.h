#pragma once

#include "driver/i2s_std.h"
#include "audio_decoder.h"
#include "esp_err.h"
#include "util.h"

#define SPK_I2S_BCLK 4
#define SPK_I2S_LRCK 0
#define SPK_I2S_DIN 15
#define SPK_AMP_SD 7

//44.1 khz
#define SPK_SAMPLE_RATE 44100

#define SPK_I2S_NUM I2S_NUM_1

typedef struct {
    i2s_chan_handle_t tx_handle;
    i2s_std_config_t std_cfg;
    esp_audio_simple_dec_handle_t decoder;
} speaker_config_t;


esp_err_t play_audio(
    buffer_u8_t *aud_buffer, 
    buffer_u8_t *pcm_buffer,
    speaker_config_t *speaker_config, 
    size_t *unconsumed_size
);

speaker_config_t *speaker_init(void);