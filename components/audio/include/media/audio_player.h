#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <esp_err.h>
#include "audio_media.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Build audio player system
 * @param player_sys Pointer to player system structure to initialize
 * @return ESP_OK on success
 */
esp_err_t audio_player_build_system(audio_player_system_t *player_sys);

/**
 * @brief Setup player for loopback test
 * @param player_sys Pointer to player system
 * @return ESP_OK on success
 */
esp_err_t audio_player_setup_loopback_test(audio_player_system_t *player_sys);

/**
 * @brief Reset player after test
 * @param player_sys Pointer to player system
 * @return ESP_OK on success
 */
esp_err_t audio_player_reset(audio_player_system_t *player_sys);

/**
 * @brief Play WAV file from SPIFFS
 * @param player_sys Pointer to player system
 * @param filename Path to WAV file (e.g., "/spiffs/sounds/starting.wav")
 * @return ESP_OK on success
 */
esp_err_t audio_player_play_wav(audio_player_system_t *player_sys, const char *filename);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_PLAYER_H