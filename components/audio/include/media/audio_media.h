#ifndef AUDIO_MEDIA_H
#define AUDIO_MEDIA_H

#include "esp_capture.h"
#include "esp_capture_sink.h"
#include "av_render.h"

#ifdef __cplusplus
extern "C" {
#endif

// Audio system structures
typedef struct {
    esp_capture_handle_t         capture_handle;
    esp_capture_audio_src_if_t *aud_src;
} audio_capture_system_t;

typedef struct {
    audio_render_handle_t audio_render;
    av_render_handle_t    player;
} audio_player_system_t;

#ifdef __cplusplus
}
#endif

#endif // AUDIO_MEDIA_H