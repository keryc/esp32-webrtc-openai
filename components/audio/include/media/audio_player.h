#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <esp_err.h>
#include <stdbool.h>
#include "audio_media.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Build audio player system
 * @param player_sys Pointer to player system structure to initialize
 * @param recorder_handle Optional recorder handle (can be NULL)
 * @return ESP_OK on success
 */
esp_err_t audio_player_build_system(audio_player_system_t *player_sys, void *recorder_handle);

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

/**
 * @brief Start recording audio to SD card
 * @param player_sys Pointer to player system
 * @return ESP_OK on success
 */
esp_err_t audio_player_start_recording(audio_player_system_t *player_sys);

/**
 * @brief Stop recording audio
 * @param player_sys Pointer to player system
 * @return ESP_OK on success
 */
esp_err_t audio_player_stop_recording(audio_player_system_t *player_sys);

/**
 * @brief Check if currently recording
 * @param player_sys Pointer to player system
 * @return true if recording, false otherwise
 */
bool audio_player_is_recording(audio_player_system_t *player_sys);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_PLAYER_H