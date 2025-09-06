#ifndef AUDIO_MODULE_H
#define AUDIO_MODULE_H

#include <esp_err.h>
#include <stdbool.h>
#include "esp_webrtc.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Audio system event callback
 */
typedef void (*audio_event_callback_t)(bool system_ready);

/**
 * @brief Initialize audio module
 * 
 * @param callback Event callback for audio system status
 * @return ESP_OK on success
 */
esp_err_t audio_module_init(audio_event_callback_t callback);

/**
 * @brief Set recorder handle for audio module
 * 
 * @param recorder_handle Handle to the recorder instance
 */
void audio_module_set_recorder_handle(void *recorder_handle);

/**
 * @brief Start audio system
 * @return ESP_OK on success
 */
esp_err_t audio_module_start(void);

/**
 * @brief Stop audio system
 * @return ESP_OK on success
 */
esp_err_t audio_module_stop(void);

/**
 * @brief Check if audio system is ready
 * @return true if ready, false otherwise
 */
bool audio_module_is_ready(void);

/**
 * @brief Get playback volume (0-100)
 * @return Current volume level
 */
int audio_module_get_volume(void);

/**
 * @brief Set playback volume (0-100)
 * @param volume Volume level to set
 * @return ESP_OK on success
 */
esp_err_t audio_module_set_volume(int volume);

/**
 * @brief Set microphone gain (0.0-100.0)
 * @param gain Gain level to set
 * @return ESP_OK on success
 */
esp_err_t audio_module_set_mic_gain(float gain);

/**
 * @brief Test audio capture and playback
 * @return ESP_OK on success
 */
esp_err_t audio_module_test_loopback(void);

/**
 * @brief Get media provider for WebRTC integration
 * @param provider Pointer to media provider structure to fill
 * @return ESP_OK on success
 */
esp_err_t audio_module_get_media_provider(esp_webrtc_media_provider_t *provider);

/**
 * @brief Temporarily release audio output resources (for feedback playback)
 * @return ESP_OK on success
 */
esp_err_t audio_module_release_output(void);

/**
 * @brief Restore audio output resources (after feedback playback)
 * @return ESP_OK on success
 */
esp_err_t audio_module_restore_output(void);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_MODULE_H