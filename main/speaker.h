#pragma once

#include "driver/i2s_std.h"

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
} speaker_config_t;

void play_audio(uint8_t *audio_buffer, int buffer_size, speaker_config_t *speaker_config);
speaker_config_t *speaker_init(void);