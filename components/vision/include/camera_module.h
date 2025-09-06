#ifndef CAMERA_MODULE_H
#define CAMERA_MODULE_H

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>
#include "esp_capture.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Camera module events
 */
typedef enum {
    CAM_EVENT_INITIALIZED,
    CAM_EVENT_FRAME_READY,
    CAM_EVENT_STREAM_STARTED,
    CAM_EVENT_STREAM_STOPPED,
    CAM_EVENT_ANALYSIS_COMPLETE,
    CAM_EVENT_ERROR
} cam_event_t;

/**
 * @brief Camera/Vision capture modes (simplified)
 */
typedef enum {
    CAM_MODE_STREAM_ONLY,   // Only HTTP preview stream
    CAM_MODE_ANALYSIS_ONLY, // Only AI analysis (no preview)
    CAM_MODE_COMBINED       // Stream + AI analysis
} cam_mode_t;

/**
 * @brief Camera/Vision quality settings
 */
typedef enum {
    CAM_QUALITY_LOW,     // 320x240, lower fps, higher compression
    CAM_QUALITY_MEDIUM,  // 640x480, medium fps
    CAM_QUALITY_HIGH,    // 800x600, higher fps, lower compression
    CAM_QUALITY_HD       // 1280x720, highest quality
} cam_quality_t;

/**
 * @brief Camera/Vision configuration
 */
typedef struct {
    cam_mode_t mode;
    cam_quality_t quality;
    uint32_t fps;                    // Target frames per second
    bool auto_exposure;
    bool auto_white_balance;
    uint8_t jpeg_quality;            // 1-100, higher is better quality
    uint32_t buffer_frames;          // Number of frames to buffer
    bool enable_live_preview;        // Enable WebRTC streaming
} cam_config_t;

/**
 * @brief Camera/Vision frame information
 */
typedef struct {
    uint8_t *data;
    size_t size;
    uint32_t width;
    uint32_t height;
    uint32_t timestamp_ms;
    uint32_t sequence_num;
    esp_capture_format_id_t format_id;
} cam_frame_t;

/**
 * @brief Camera/Vision statistics
 */
typedef struct {
    uint32_t total_frames_captured;
    uint32_t frames_dropped;
    uint32_t current_fps;
    uint32_t buffer_usage_percent;
    bool is_streaming;
    bool is_recording;
    uint64_t total_bytes_processed;
} cam_stats_t;

/**
 * @brief Camera/Vision event callback
 */
typedef void (*cam_event_callback_t)(cam_event_t event, void *data);

/**
 * @brief Initialize unified camera/vision module
 * 
 * @param config Module configuration
 * @param callback Event callback function
 * @return ESP_OK on success
 */
esp_err_t cam_module_init(const cam_config_t *config, cam_event_callback_t callback);

/**
 * @brief Start camera capture and streaming
 * 
 * @param mode Capture mode to start
 * @return ESP_OK on success
 */
esp_err_t cam_module_start(cam_mode_t mode);

/**
 * @brief Stop camera capture and streaming
 * 
 * @return ESP_OK on success
 */
esp_err_t cam_module_stop(void);

/**
 * @brief Get current frame for analysis or preview
 * 
 * @param frame Output frame structure
 * @param timeout_ms Timeout in milliseconds (0 for no wait)
 * @return ESP_OK on success
 */

/**
 * @brief Start live preview streaming to laptop
 * 
 * @param stream_url WebRTC stream URL or endpoint
 * @return ESP_OK on success
 */
esp_err_t cam_module_start_preview_stream(const char *stream_url);

/**
 * @brief Stop live preview streaming
 * 
 * @return ESP_OK on success
 */
esp_err_t cam_module_stop_preview_stream(void);




/**
 * @brief Start continuous capture for vision buffer
 * 
 * @return ESP_OK on success
 */
esp_err_t cam_module_start_capture(void);

/**
 * @brief Stop continuous capture
 * 
 * @return ESP_OK on success
 */
esp_err_t cam_module_stop_capture(void);

/**
 * @brief Recording configuration for future video capture
 */
typedef struct {
    bool save_to_storage;     // Save frames to SD/Flash
    char filepath[64];        // Path for saving
    uint32_t max_frames;      // Maximum frames to record
    bool circular_buffer;     // DVR mode - keep last N frames
    uint32_t fps;            // Target FPS for recording
} cam_recording_config_t;

/**
 * @brief Start recording with configuration (future use)
 * 
 * @param config Recording configuration
 * @return ESP_OK on success
 */
esp_err_t cam_module_start_recording(const cam_recording_config_t *config);

/**
 * @brief Set capture interval for continuous mode
 * 
 * @param interval_ms Interval between captures in milliseconds
 * @return ESP_OK on success
 */
esp_err_t cam_module_set_capture_interval(uint32_t interval_ms);

/**
 * @brief Check if continuous capture is active
 * 
 * @return true if capturing, false otherwise
 */
bool cam_module_is_capturing(void);

/**
 * @brief Get capture statistics for recording
 * 
 * @param frames_captured Output: number of frames captured
 * @param frames_dropped Output: number of frames dropped
 * @return ESP_OK on success
 */
esp_err_t cam_module_get_capture_stats(uint32_t *frames_captured, uint32_t *frames_dropped);

/**
 * @brief Set camera quality and resolution
 * 
 * @param quality New quality setting
 * @return ESP_OK on success
 */
esp_err_t cam_module_set_quality(cam_quality_t quality);

/**
 * @brief Set capture frame rate
 * 
 * @param fps Target frames per second
 * @return ESP_OK on success
 */
esp_err_t cam_module_set_fps(uint32_t fps);

/**
 * @brief Get module statistics
 * 
 * @param stats Output statistics structure
 * @return ESP_OK on success
 */
esp_err_t cam_module_get_stats(cam_stats_t *stats);

/**
 * @brief Test camera capture
 * 
 * @return ESP_OK on success
 */
esp_err_t cam_module_test_capture(void);

/**
 * @brief Check if module is ready and active
 * 
 * @return true if ready, false otherwise
 */
bool cam_module_is_ready(void);

/**
 * @brief Deinitialize camera/vision module
 * 
 * @return ESP_OK on success
 */
esp_err_t cam_module_deinit(void);

/**
 * @brief Get frames on-demand for vision analysis (battery efficient)
 * 
 * @param max_frames Maximum number of frames to capture
 * @param frame_count Output: actual number of frames captured
 * @return Array of allocated base64 strings (each must be freed) or NULL
 */
char** cam_module_get_vision_frames(int max_frames, int *frame_count);

#ifdef __cplusplus
}
#endif

#endif // CAMERA_MODULE_H
