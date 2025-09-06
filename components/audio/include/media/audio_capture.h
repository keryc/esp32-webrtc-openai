#ifndef AUDIO_CAPTURE_H
#define AUDIO_CAPTURE_H

#include <esp_err.h>
#include "audio_media.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Build audio capture system
 * @param capture_sys Pointer to capture system structure to initialize
 * @return ESP_OK on success
 */
esp_err_t audio_capture_build_system(audio_capture_system_t *capture_sys);

/**
 * @brief Start capture test for loopback
 * @param capture_sys Pointer to capture system
 * @param primary_path_out Output path handle for primary capture
 * @return ESP_OK on success
 */
esp_err_t audio_capture_start_loopback_test(audio_capture_system_t *capture_sys, 
                                           esp_capture_sink_handle_t *primary_path_out);

/**
 * @brief Stop capture test
 * @param capture_sys Pointer to capture system
 * @return ESP_OK on success
 */
esp_err_t audio_capture_stop_loopback_test(audio_capture_system_t *capture_sys);

/**
 * @brief Set microphone gain (software controlled for I2S microphones)
 * @param gain_percent Gain percentage (0-100)
 * @return ESP_OK on success
 */
esp_err_t audio_capture_set_mic_gain(float gain_percent);

/**
 * @brief Get optimal settings for current microphone type
 * @param sample_rate Output sample rate
 * @param bit_depth Output bit depth
 * @param channels Output channels
 * @return ESP_OK on success
 */
esp_err_t audio_capture_get_optimal_settings(uint32_t *sample_rate, uint8_t *bit_depth, uint8_t *channels);

/**
 * @brief Get optimal settings based on codec capabilities
 * @param codec_name Name of the codec (e.g., "OPUS", "PCM", "AAC")
 * @param sample_rate Output sample rate
 * @param bit_depth Output bit depth
 * @param channels Output channels
 * @return ESP_OK on success
 */
esp_err_t audio_capture_get_codec_optimal_settings(const char *codec_name, uint32_t *sample_rate, uint8_t *bit_depth, uint8_t *channels);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_CAPTURE_H
