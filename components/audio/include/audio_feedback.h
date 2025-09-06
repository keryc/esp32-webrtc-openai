#ifndef AUDIO_FEEDBACK_H
#define AUDIO_FEEDBACK_H

#include <esp_err.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Audio feedback types
 */
typedef enum {
    AUDIO_FEEDBACK_TOUCH_START,     /**< Touch start sound */
    AUDIO_FEEDBACK_TOUCH_CONFIRM,   /**< Touch confirmation */
    AUDIO_FEEDBACK_SYSTEM_READY,    /**< System ready */
    AUDIO_FEEDBACK_ERROR           /**< Error sound */
} audio_feedback_type_t;

/**
 * @brief Audio feedback completion callback
 */
typedef void (*audio_feedback_callback_t)(audio_feedback_type_t type, bool success);

/**
 * @brief Audio feedback WAV completion callback  
 */
typedef void (*audio_feedback_wav_callback_t)(const char *filename, bool success);

/**
 * @brief Initialize audio feedback system
 * 
 * @return ESP_OK on success
 */
esp_err_t audio_feedback_init(void);

/**
 * @brief Play audio feedback
 * 
 * @param type Type of feedback to play
 * @param callback Optional callback when playback completes
 * @return ESP_OK on success
 */
esp_err_t audio_feedback_play(audio_feedback_type_t type, audio_feedback_callback_t callback);

/**
 * @brief Play WAV file from SPIFFS (asynchronous)
 * 
 * @param filename Path to WAV file (e.g., "/spiffs/sounds/starting.wav")
 * @param callback Optional callback when playback completes
 * @return ESP_OK on success
 */
esp_err_t audio_feedback_play_wav(const char *filename, audio_feedback_wav_callback_t callback);

/**
 * @brief Stop current audio feedback
 * 
 * @return ESP_OK on success
 */
esp_err_t audio_feedback_stop(void);

/**
 * @brief Check if audio feedback is currently playing
 * 
 * @return true if playing, false otherwise
 */
bool audio_feedback_is_playing(void);

/**
 * @brief Set volume for audio feedback (0-100)
 * 
 * @param volume Volume level
 * @return ESP_OK on success
 */
esp_err_t audio_feedback_set_volume(uint8_t volume);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_FEEDBACK_H