#ifndef OPENAI_CLIENT_H
#define OPENAI_CLIENT_H

#include <esp_err.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start OpenAI WebRTC session
 * @return ESP_OK on success
 */
esp_err_t openai_realtime_start(void);

/**
 * @brief Stop OpenAI WebRTC session
 * @return ESP_OK on success
 */
esp_err_t openai_realtime_stop(void);

/**
 * @brief Send text message to OpenAI
 * @param text Text message to send
 * @return ESP_OK on success
 */
esp_err_t openai_realtime_send_text(const char *text);

/**
 * @brief Query WebRTC status
 * @return ESP_OK on success
 */
esp_err_t openai_realtime_query(void);

/**
 * @brief Check if WebRTC is connected
 * @return true if connected, false otherwise
 */
bool openai_realtime_is_connected(void);

/**
 * @brief Pause WebRTC audio (keeps connection alive)
 * @return ESP_OK on success
 */
esp_err_t openai_realtime_pause_audio(void);

/**
 * @brief Resume WebRTC audio
 * @return ESP_OK on success
 */
esp_err_t openai_realtime_resume_audio(void);

/**
 * @brief Set activation mode for OpenAI instructions
 * @param vision_enabled true for audio+vision mode, false for audio-only mode
 * @return ESP_OK on success
 */
esp_err_t openai_realtime_set_activation_mode(bool vision_enabled);

#ifdef __cplusplus
}
#endif

#endif // OPENAI_CLIENT_H