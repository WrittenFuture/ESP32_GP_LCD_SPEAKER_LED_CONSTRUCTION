#pragma once

#include "esp_audio_simple_dec.h"

esp_audio_err_t mp3_decoder_init(
    esp_audio_simple_dec_handle_t *decoder
);