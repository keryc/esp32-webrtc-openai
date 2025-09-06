#include "camera_module.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <string.h>
#include <stdlib.h>
#include "memory_manager.h"
#include "mbedtls/base64.h"
#include "camera_preview_server.h"
#include "esp_camera.h"
#include "vision_utils.h"
#include "codec_board.h"

static const char *TAG = "cam_module";

// Module state
static struct {
    bool initialized;
    bool streaming;
    cam_config_t config;
    cam_event_callback_t event_callback;
    
    // Camera configuration
    camera_config_t camera_config;
    bool camera_initialized;
    
    // Frame management - queue removed, using on-demand capture
    SemaphoreHandle_t stats_mutex;
    cam_stats_t stats;
    
    // Tasks
    TaskHandle_t capture_task_handle;
} cam_state = {0};

// Convert quality enum to camera settings
static void quality_to_camera_settings(cam_quality_t quality, 
                                       framesize_t *framesize, 
                                       uint8_t *jpeg_quality)
{
    switch (quality) {
        case CAM_QUALITY_LOW:
            *framesize = FRAMESIZE_QVGA; *jpeg_quality = 15;
            break;
        case CAM_QUALITY_MEDIUM:
            *framesize = FRAMESIZE_VGA; *jpeg_quality = 12;
            break;
        case CAM_QUALITY_HIGH:
            *framesize = FRAMESIZE_SVGA; *jpeg_quality = 10;
            break;
        case CAM_QUALITY_HD:
            *framesize = FRAMESIZE_HD; *jpeg_quality = 8;
            break;
    }
}

// Camera capture task
static void camera_capture_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Camera capture task started");
    
    uint32_t frame_interval_ms = 1000 / cam_state.config.fps;
    TickType_t last_capture = 0;
    
    // FPS calculation variables
    uint32_t fps_frame_count = 0;
    TickType_t fps_last_update = xTaskGetTickCount();
    
    while (cam_state.streaming) {
        TickType_t now = xTaskGetTickCount();
        
        // Throttle capture rate
        if (now - last_capture >= pdMS_TO_TICKS(frame_interval_ms)) {
            camera_fb_t *fb = esp_camera_fb_get();
            if (fb != NULL) {
                // Increment frame count for FPS calculation
                fps_frame_count++;
                
                // Update statistics
                if (xSemaphoreTake(cam_state.stats_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                    cam_state.stats.total_frames_captured++;
                    cam_state.stats.total_bytes_processed += fb->len;
                    
                    // Update FPS every second
                    if (now - fps_last_update >= pdMS_TO_TICKS(1000)) {
                        cam_state.stats.current_fps = fps_frame_count;
                        fps_frame_count = 0;
                        fps_last_update = now;
                    }
                    
                    xSemaphoreGive(cam_state.stats_mutex);
                }
                
                // Send frame to HTTP preview server if stream mode is enabled
                if (cam_state.config.mode == CAM_MODE_STREAM_ONLY || 
                    cam_state.config.mode == CAM_MODE_COMBINED) {
                    static TickType_t last_preview_frame = 0;
                    if (now - last_preview_frame >= pdMS_TO_TICKS(200)) { // 5 FPS max for bandwidth
                        camera_preview_server_send_frame(fb->buf, fb->len);
                        last_preview_frame = now;
                    }
                }
                
                // Notify frame ready
                if (cam_state.event_callback) {
                    cam_frame_t cv_frame = {
                        .data = fb->buf,
                        .size = fb->len,
                        .width = fb->width,
                        .height = fb->height,
                        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000),
                        .sequence_num = cam_state.stats.total_frames_captured,
                        .format_id = ESP_CAPTURE_FMT_ID_MJPEG
                    };
                    cam_state.event_callback(CAM_EVENT_FRAME_READY, &cv_frame);
                }
                
                esp_camera_fb_return(fb);
                last_capture = now;
            } else {
                // Camera error
                if (xSemaphoreTake(cam_state.stats_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                    cam_state.stats.frames_dropped++;
                    xSemaphoreGive(cam_state.stats_mutex);
                }
            }
        }
        
        // Dynamic delay based on frame interval to reduce CPU usage and mutex contention
        uint32_t delay_ms = frame_interval_ms / 4; // Quarter of frame interval
        if (delay_ms < 10) delay_ms = 10; // Minimum 10ms
        if (delay_ms > 50) delay_ms = 50; // Maximum 50ms
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
    
    ESP_LOGI(TAG, "Camera capture task ended");
    cam_state.capture_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t cam_module_init(const cam_config_t *config, cam_event_callback_t callback)
{
    if (cam_state.initialized) {
        ESP_LOGW(TAG, "Camera/Vision module already initialized - reinitializing...");
        // Deinit first before reinitializing
        cam_module_deinit();
    }
    
    if (!config) {
        ESP_LOGE(TAG, "Invalid configuration");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Initializing unified Camera/Vision module");
    
    // Store configuration and callback
    memcpy(&cam_state.config, config, sizeof(cam_config_t));
    cam_state.event_callback = callback;
    
    // Create statistics mutex
    cam_state.stats_mutex = xSemaphoreCreateMutex();
    if (!cam_state.stats_mutex) {
        ESP_LOGE(TAG, "Failed to create statistics mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // Initialize statistics
    memset(&cam_state.stats, 0, sizeof(cam_stats_t));
    
    // Get camera configuration from codec_board
    camera_cfg_t board_cam_cfg;
    codec_i2c_pin_t i2c_pin;
    
    // Get camera configuration from board definition
    esp_err_t ret = get_camera_cfg(&board_cam_cfg);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to get camera configuration from board");
        goto cleanup;
    }
    
    ESP_LOGI(TAG, "Using camera config from board definition");
    
    // Get I2C configuration for camera SCCB (port 0 is typically used for camera)
    if (get_i2c_pin(0, &i2c_pin) != 0) {
        ESP_LOGE(TAG, "Failed to get I2C pin configuration");
        goto cleanup;
    }
    
    // Configure camera quality settings
    framesize_t framesize = FRAMESIZE_VGA; // Default to MEDIUM quality
    uint8_t jpeg_quality = 12;             // Default to MEDIUM quality
    quality_to_camera_settings(config->quality, &framesize, &jpeg_quality);
    
    // Build camera configuration from board definition
    cam_state.camera_config = (camera_config_t){
        .pin_pwdn = board_cam_cfg.pwr,
        .pin_reset = board_cam_cfg.reset,
        .pin_xclk = board_cam_cfg.xclk,
        .pin_sccb_sda = i2c_pin.sda,
        .pin_sccb_scl = i2c_pin.scl,
        .pin_d7 = board_cam_cfg.data[7],
        .pin_d6 = board_cam_cfg.data[6],
        .pin_d5 = board_cam_cfg.data[5],
        .pin_d4 = board_cam_cfg.data[4],
        .pin_d3 = board_cam_cfg.data[3],
        .pin_d2 = board_cam_cfg.data[2],
        .pin_d1 = board_cam_cfg.data[1],
        .pin_d0 = board_cam_cfg.data[0],
        .pin_vsync = board_cam_cfg.vsync,
        .pin_href = board_cam_cfg.href,
        .pin_pclk = board_cam_cfg.pclk,
        .xclk_freq_hz = 20000000,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size = framesize,
        .jpeg_quality = jpeg_quality,
        .fb_count = 2,
        .grab_mode = CAMERA_GRAB_LATEST,
        .fb_location = CAMERA_FB_IN_PSRAM
    };
    
    ESP_LOGI(TAG, "Camera pins - XCLK:%d, SDA:%d, SCL:%d, D0-7:[%d,%d,%d,%d,%d,%d,%d,%d], VSYNC:%d, HREF:%d, PCLK:%d",
             cam_state.camera_config.pin_xclk,
             cam_state.camera_config.pin_sccb_sda,
             cam_state.camera_config.pin_sccb_scl,
             cam_state.camera_config.pin_d0,
             cam_state.camera_config.pin_d1,
             cam_state.camera_config.pin_d2,
             cam_state.camera_config.pin_d3,
             cam_state.camera_config.pin_d4,
             cam_state.camera_config.pin_d5,
             cam_state.camera_config.pin_d6,
             cam_state.camera_config.pin_d7,
             cam_state.camera_config.pin_vsync,
             cam_state.camera_config.pin_href,
             cam_state.camera_config.pin_pclk);  
    
    // Initialize camera
    ret = esp_camera_init(&cam_state.camera_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    cam_state.camera_initialized = true;
    cam_state.initialized = true;
    
    // Initialize preview server for laptop viewing
    if (config->enable_live_preview) {
        ret = camera_preview_server_init(CONFIG_AG_VISION_PREVIEW_PORT);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Camera preview server initialized on port %d", CONFIG_AG_VISION_PREVIEW_PORT);
        } else {
            ESP_LOGW(TAG, "Failed to initialize preview server: %s", esp_err_to_name(ret));
        }
    }
    
    // Notify initialization complete
    if (cam_state.event_callback) {
        cam_state.event_callback(CAM_EVENT_INITIALIZED, NULL);
    }
    
    ESP_LOGI(TAG, "Camera/Vision module initialized successfully");
    return ESP_OK;
    
cleanup:
    if (cam_state.stats_mutex) {
        vSemaphoreDelete(cam_state.stats_mutex);
    }
    return ESP_FAIL;
}

esp_err_t cam_module_start(cam_mode_t mode)
{
    if (!cam_state.initialized || !cam_state.camera_initialized) {
        ESP_LOGE(TAG, "Module not initialized");
        return ESP_FAIL;
    }
    
    if (cam_state.streaming) {
        ESP_LOGW(TAG, "Already streaming");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Starting camera/vision capture (mode: %d)", mode);
    
    cam_state.config.mode = mode;
    cam_state.streaming = true;
    cam_state.stats.is_streaming = true;
    
    // Start capture task
    BaseType_t ret = xTaskCreate(camera_capture_task, "cam_capture", 8192, NULL, 5, 
                                &cam_state.capture_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create capture task");
        cam_state.streaming = false;
        cam_state.stats.is_streaming = false;
        return ESP_FAIL;
    }
    
    // Notify streaming started
    if (cam_state.event_callback) {
        cam_state.event_callback(CAM_EVENT_STREAM_STARTED, &mode);
    }
    
    ESP_LOGI(TAG, "Camera/Vision capture started successfully");
    return ESP_OK;
}

esp_err_t cam_module_stop(void)
{
    if (!cam_state.initialized) {
        ESP_LOGE(TAG, "Module not initialized");
        return ESP_FAIL;
    }
    
    if (!cam_state.streaming) {
        ESP_LOGW(TAG, "Not streaming");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Stopping camera/vision capture");
    
    cam_state.streaming = false;
    cam_state.stats.is_streaming = false;
    
    // Wait for task to finish
    if (cam_state.capture_task_handle) {
        for (int i = 0; i < 100 && cam_state.capture_task_handle; i++) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    // Notify streaming stopped
    if (cam_state.event_callback) {
        cam_state.event_callback(CAM_EVENT_STREAM_STOPPED, NULL);
    }
    
    ESP_LOGI(TAG, "Camera/Vision capture stopped");
    return ESP_OK;
}

esp_err_t cam_module_start_preview_stream(const char *stream_url)
{
    ESP_LOGI(TAG, "Starting preview stream to: %s", stream_url ? stream_url : "HTTP Server");
    
    // First, ensure the preview server is initialized
    esp_err_t ret = camera_preview_server_init(CONFIG_AG_VISION_PREVIEW_PORT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize preview server: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Ensure camera is streaming with appropriate mode
    if (!cam_state.streaming) {
        ESP_LOGI(TAG, "Starting camera streaming for preview");
        ret = cam_module_start(CAM_MODE_STREAM_ONLY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start camera streaming");
            return ret;
        }
    } else if (cam_state.config.mode == CAM_MODE_ANALYSIS_ONLY) {
        // Switch to combined mode to enable streaming
        ESP_LOGI(TAG, "Switching to combined mode for preview");
        cam_state.config.mode = CAM_MODE_COMBINED;
    }
    
    // Now start the HTTP server
    ret = camera_preview_server_start();
    if (ret == ESP_OK) {
        char url[128];
        if (camera_preview_server_get_url(url, sizeof(url)) == ESP_OK) {
            ESP_LOGI(TAG, "Preview stream available at: %s", url);
            ESP_LOGI(TAG, "Open this URL in your laptop browser to view live camera feed");
        }
    }
    
    return ret;
}

esp_err_t cam_module_stop_preview_stream(void)
{
    ESP_LOGI(TAG, "Stopping preview stream");
    return camera_preview_server_stop();
}


esp_err_t cam_module_set_quality(cam_quality_t quality)
{
    if (!cam_state.initialized) {
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Setting quality to: %d", quality);
    cam_state.config.quality = quality;
    
    // Update camera settings
    framesize_t framesize = FRAMESIZE_VGA; // Default to MEDIUM quality
    uint8_t jpeg_quality = 12;             // Default to MEDIUM quality
    quality_to_camera_settings(quality, &framesize, &jpeg_quality);
    
    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor) {
        sensor->set_framesize(sensor, framesize);
        sensor->set_quality(sensor, jpeg_quality);
    }
    
    return ESP_OK;
}

esp_err_t cam_module_set_fps(uint32_t fps)
{
    if (!cam_state.initialized) {
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Setting FPS to: %lu", fps);
    cam_state.config.fps = fps;
    
    return ESP_OK;
}

esp_err_t cam_module_get_stats(cam_stats_t *stats)
{
    if (!cam_state.initialized || !stats) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(cam_state.stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(stats, &cam_state.stats, sizeof(cam_stats_t));
        
        // Buffer usage no longer relevant without queue
        stats->buffer_usage_percent = 0;
        
        xSemaphoreGive(cam_state.stats_mutex);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

esp_err_t cam_module_test_capture(void)
{
    if (!cam_state.initialized) {
        ESP_LOGE(TAG, "Module not initialized");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Testing camera capture...");
    
    // Try to capture a single frame
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) {
        ESP_LOGI(TAG, "Test successful - captured %zu bytes (%dx%d)", 
                 fb->len, fb->width, fb->height);
        esp_camera_fb_return(fb);
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Test failed - could not capture frame");
        return ESP_FAIL;
    }
}

bool cam_module_is_ready(void)
{
    return cam_state.initialized && cam_state.camera_initialized;
}

esp_err_t cam_module_deinit(void)
{
    if (!cam_state.initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Deinitializing Camera/Vision module");
    
    // Stop streaming if active
    if (cam_state.streaming) {
        cam_module_stop();
    }
    
    // Deinit camera
    if (cam_state.camera_initialized) {
        esp_camera_deinit();
        cam_state.camera_initialized = false;
    }
    
    // Clean up mutex
    if (cam_state.stats_mutex) {
        vSemaphoreDelete(cam_state.stats_mutex);
        cam_state.stats_mutex = NULL;
    }
    
    // Reset state
    memset(&cam_state, 0, sizeof(cam_state));
    
    ESP_LOGI(TAG, "Camera/Vision module deinitialized");
    return ESP_OK;
}

// Recording/capture statistics  
static struct {
    uint32_t capture_interval_ms;
    uint32_t total_captured_for_recording;
    uint32_t total_dropped_for_recording;
} recording_stats = {
    .capture_interval_ms = CONFIG_AG_VISION_CAPTURE_INTERVAL_MS,
    .total_captured_for_recording = 0,
    .total_dropped_for_recording = 0
};

esp_err_t cam_module_start_capture(void)
{
    // Simply start streaming in analysis mode for continuous capture
    ESP_LOGI(TAG, "Starting continuous capture mode");
    return cam_module_start(CAM_MODE_ANALYSIS_ONLY);
}

esp_err_t cam_module_stop_capture(void)
{
    // Simply stop streaming
    ESP_LOGI(TAG, "Stopping continuous capture");
    return cam_module_stop();
}


esp_err_t cam_module_start_recording(const cam_recording_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "📹 Starting recording: fps=%lu, max_frames=%lu, circular=%d", 
             config->fps, config->max_frames, config->circular_buffer);
    
    // Start capture with configured FPS
    esp_err_t ret = cam_module_start_capture();
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Set interval based on FPS (1000ms / fps = interval)
    uint32_t interval = 1000 / config->fps;
    cam_module_set_capture_interval(interval);
    
    // TODO: Future implementation
    // - Setup SD card saving if config->save_to_storage
    // - Configure circular buffer if needed
    // - Set max frames limit
    
    ESP_LOGI(TAG, "Recording started with %lu ms interval", interval);
    return ESP_OK;
}

esp_err_t cam_module_get_capture_stats(uint32_t *frames_captured, uint32_t *frames_dropped)
{
    if (frames_captured) {
        *frames_captured = recording_stats.total_captured_for_recording;
    }
    if (frames_dropped) {
        *frames_dropped = recording_stats.total_dropped_for_recording;
    }
    return ESP_OK;
}

esp_err_t cam_module_set_capture_interval(uint32_t interval_ms)
{
    if (interval_ms < 10) interval_ms = 10; // Minimum reasonable interval
    
    recording_stats.capture_interval_ms = interval_ms;
    ESP_LOGI(TAG, "Set capture interval to %u ms", (unsigned)interval_ms);
    
    return ESP_OK;
}

bool cam_module_is_capturing(void)
{
    // Return true if streaming in analysis mode
    return cam_state.streaming && 
           (cam_state.config.mode == CAM_MODE_ANALYSIS_ONLY || 
            cam_state.config.mode == CAM_MODE_COMBINED);
}


// Vision frame capture implementation (battery efficient on-demand)
char** cam_module_get_vision_frames(int max_frames, int *frame_count)
{
    if (!cam_state.initialized || !cam_state.camera_initialized) {
        ESP_LOGE(TAG, "Camera module not initialized (init:%d, camera:%d)", 
                 cam_state.initialized, cam_state.camera_initialized);
        if (frame_count) *frame_count = 0;
        return NULL;
    }
    
    if (max_frames <= 0 || max_frames > 5) {
        ESP_LOGW(TAG, "Invalid max_frames: %d (limiting to 1-5)", max_frames);
        max_frames = (max_frames > 5) ? 5 : 1;
    }
    
    ESP_LOGI(TAG, "📸 Starting on-demand capture of %d frames", max_frames);
    uint32_t start_time = (uint32_t)(esp_timer_get_time() / 1000);
    
    // Allocate array for frame pointers
    char **frames = mem_alloc(sizeof(char*) * max_frames, 
                             MEM_POLICY_PREFER_PSRAM, "ondemand_frame_array");
    if (!frames) {
        ESP_LOGE(TAG, "Failed to allocate frame array");
        if (frame_count) *frame_count = 0;
        return NULL;
    }
    
    int actual_count = 0;
    
    // Capture and encode frames on-demand
    for (int i = 0; i < max_frames; i++) {
        uint32_t frame_start = (uint32_t)(esp_timer_get_time() / 1000);
        
        // Get fresh frame directly from camera hardware
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGW(TAG, "Failed to capture frame %d", i + 1);
            continue;
        }
        
        uint32_t capture_time = (uint32_t)(esp_timer_get_time() / 1000) - frame_start;
        ESP_LOGI(TAG, "Frame %d captured in %u ms (size: %zu bytes)", i + 1, (unsigned)capture_time, fb->len);
        
        // Encode to base64
        uint32_t encode_start = (uint32_t)(esp_timer_get_time() / 1000);
        size_t output_len = 0;
        unsigned char *base64_data = mem_alloc((fb->len * 4 / 3) + 16, 
                                              MEM_POLICY_PREFER_PSRAM, "base64_encode");
        
        if (base64_data) {
            int ret = mbedtls_base64_encode(base64_data, (fb->len * 4 / 3) + 16, 
                                           &output_len, fb->buf, fb->len);
            
            if (ret == 0) {
                frames[actual_count] = (char *)base64_data;
                actual_count++;
                
                uint32_t encode_time = (uint32_t)(esp_timer_get_time() / 1000) - encode_start;
                ESP_LOGI(TAG, "Frame %d encoded in %u ms (size: %zu -> %zu bytes)", 
                        i + 1, (unsigned)encode_time, fb->len, output_len);
            } else {
                ESP_LOGW(TAG, "Failed to encode frame %d to base64", i + 1);
                mem_free(base64_data);
            }
        }
        
        // Return the frame buffer to the camera
        esp_camera_fb_return(fb);
        
        // Small delay between captures to avoid buffer issues
        if (i < max_frames - 1) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
    
    uint32_t total_time = (uint32_t)(esp_timer_get_time() / 1000) - start_time;
    ESP_LOGI(TAG, "⏱️ On-demand capture completed: %d/%d frames in %u ms", 
            actual_count, max_frames, (unsigned)total_time);
    
    if (actual_count == 0) {
        mem_free(frames);
        frames = NULL;
    }
    
    if (frame_count) {
        *frame_count = actual_count;
    }
    
    return frames;
}
