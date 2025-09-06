#ifndef WEBRTC_MODULE_H
#define WEBRTC_MODULE_H

#include <esp_err.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WebRTC connection states
 */
typedef enum {
    WEBRTC_STATE_DISCONNECTED,
    WEBRTC_STATE_CONNECTING,
    WEBRTC_STATE_CONNECTED,
    WEBRTC_STATE_FAILED
} webrtc_state_t;

/**
 * @brief WebRTC event callback
 */
typedef void (*webrtc_event_callback_t)(webrtc_state_t state);

/**
 * @brief Initialize WebRTC module
 * 
 * @param callback Event callback for WebRTC state changes
 * @return ESP_OK on success
 */
esp_err_t webrtc_module_init(webrtc_event_callback_t callback);

/**
 * @brief Start WebRTC session
 * @return ESP_OK on success
 */
esp_err_t webrtc_module_start(void);

/**
 * @brief Stop WebRTC session
 * @return ESP_OK on success
 */
esp_err_t webrtc_module_stop(void);

/**
 * @brief Get current WebRTC state
 * @return Current WebRTC state
 */
webrtc_state_t webrtc_module_get_state(void);

/**
 * @brief Send text message to OpenAI
 * @param text Text message to send
 * @return ESP_OK on success
 */
esp_err_t webrtc_module_send_text(const char *text);

/**
 * @brief Query WebRTC status (for monitoring)
 * @return ESP_OK on success
 */
esp_err_t webrtc_module_query_status(void);

/**
 * @brief Check if WebRTC is connected
 * @return true if connected, false otherwise
 */
bool webrtc_module_is_connected(void);

/**
 * @brief Pause WebRTC audio (keeps connection alive)
 * @return ESP_OK on success
 */
esp_err_t webrtc_module_pause_audio(void);

/**
 * @brief Resume WebRTC audio
 * @return ESP_OK on success
 */
esp_err_t webrtc_module_resume_audio(void);

#ifdef __cplusplus
}
#endif

#endif // WEBRTC_MODULE_H