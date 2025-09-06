#include "thread_scheduler.h"
#include <esp_log.h>
#include <string.h>
#include "media_lib_adapter.h"
#include "media_lib_os.h"
#include "esp_capture.h"
#include "sdkconfig.h"

static const char *TAG = "thread_scheduler";

/**
 * @brief Global thread scheduler for all system tasks
 * 
 * This scheduler configures optimal stack sizes and CPU core assignments
 * for all media and WebRTC related tasks.
 */
static void global_thread_scheduler(const char *thread_name, media_lib_thread_cfg_t *schedule_cfg)
{
    // ========== Video Tasks ==========
    // Video encoding - for H264
    if (strcmp(thread_name, "venc_0") == 0) {
        schedule_cfg->priority = 10;
#if CONFIG_IDF_TARGET_ESP32S3
        schedule_cfg->stack_size = 20 * 1024;  // 20KB for S3 hardware encoder
#else
        schedule_cfg->stack_size = 8 * 1024;
#endif
        schedule_cfg->core_id = 1;
    }
    
    // ========== WebRTC Tasks ==========
    // WebRTC peer connection - needs large stack for complex operations
    else if (strcmp(thread_name, "pc_task") == 0) {
        schedule_cfg->stack_size = 35 * 1024;  // 35KB stack (INCREASED to prevent crashes)
        schedule_cfg->priority = 18;           // High priority
        schedule_cfg->core_id = 1;             // Core 1 for networking
    }
    // WebRTC peer connection send
    else if (strcmp(thread_name, "pc_send") == 0) {
        schedule_cfg->stack_size = 4 * 1024;   // 4KB stack
        schedule_cfg->priority = 15;           // Medium-high priority
        schedule_cfg->core_id = 1;             // Core 1 for networking
    }
    // Start task for async operations
    else if (strcmp(thread_name, "start") == 0) {
        schedule_cfg->stack_size = 6 * 1024;   // 6KB stack
        schedule_cfg->priority = 5;            // Low priority
        schedule_cfg->core_id = 0;             // Core 0
    }
    // WebRTC initialization tasks
    else if (strcmp(thread_name, "webrtc_start") == 0 || strcmp(thread_name, "webrtc_stop") == 0) {
        schedule_cfg->stack_size = 8 * 1024;   // 8KB stack
        schedule_cfg->priority = 5;            // Low priority
        schedule_cfg->core_id = 0;             // Core 0
    }
    // Vision initialization task
    else if (strcmp(thread_name, "vision_init") == 0) {
        schedule_cfg->stack_size = 6 * 1024;   // 6KB stack
        schedule_cfg->priority = 4;            // Lower priority than WebRTC
        schedule_cfg->core_id = 1;             // Core 1 for camera processing
    }
    
    // ========== Audio Tasks ==========
    // Audio encoding - OPUS needs huge stack
#ifdef CONFIG_AG_WEBRTC_SUPPORT_OPUS
    else if (strcmp(thread_name, "aenc_0") == 0) {
        // For OPUS encoder it needs huge stack, when using G711 can set it to small value
        schedule_cfg->stack_size = 40 * 1024;  // 40KB stack for OPUS
        schedule_cfg->priority = 10;           // Medium priority
        schedule_cfg->core_id = 1;             // Core 1 for audio processing
    }
    else if (strcmp(thread_name, "buffer_in") == 0) {
        schedule_cfg->stack_size = 6 * 1024;   // 6KB stack
        schedule_cfg->priority = 10;           // Medium priority
        schedule_cfg->core_id = 0;             // Core 0 for I/O
    }
#endif
    // Audio source reading - AUD_SRC thread
    else if (strcmp(thread_name, "AUD_SRC") == 0) {
#ifdef CONFIG_AG_WEBRTC_SUPPORT_OPUS
        schedule_cfg->stack_size = 40 * 1024;  // 40KB stack for OPUS
#endif
        schedule_cfg->priority = 15;           // High priority for timing
        schedule_cfg->core_id = 0;             // Core 0 for I/O (default from original)
    }
    // Audio decoding - critical for real-time audio
    else if (strcmp(thread_name, "Adec") == 0) {
        schedule_cfg->stack_size = 40 * 1024;  // 40KB stack
        schedule_cfg->priority = 15;           // High priority
        schedule_cfg->core_id = 0;             // Core 0 for audio processing
    }
    // Audio render - highest priority for smooth playback
    else if (strcmp(thread_name, "ARender") == 0) {
        schedule_cfg->stack_size = 8 * 1024;   // 8KB stack
        schedule_cfg->priority = 20;           // Highest priority
        schedule_cfg->core_id = 0;             // Core 0 for audio render
    }
    
    // ========== Default Configuration ==========
    else {
        // Default configuration for unknown threads
        schedule_cfg->stack_size = 4 * 1024;   // 4KB default
        schedule_cfg->priority = 5;            // Low priority default
        schedule_cfg->core_id = 0;             // Core 0 default
        ESP_LOGW(TAG, "Unknown thread '%s', using default config", thread_name);
    }
    
    ESP_LOGI(TAG, "Thread '%s': stack=%lu, priority=%d, core=%d", 
             thread_name, schedule_cfg->stack_size, schedule_cfg->priority, schedule_cfg->core_id);
}

/**
 * @brief Capture scheduler for esp_capture tasks
 */
static void capture_scheduler(const char *name, esp_capture_thread_schedule_cfg_t *schedule_cfg)
{
    media_lib_thread_cfg_t cfg = {
        .stack_size = schedule_cfg->stack_size,
        .priority = schedule_cfg->priority,
        .core_id = schedule_cfg->core_id,
    };
    schedule_cfg->stack_in_ext = true;
    global_thread_scheduler(name, &cfg);
    schedule_cfg->stack_size = cfg.stack_size;
    schedule_cfg->priority = cfg.priority;
    schedule_cfg->core_id = cfg.core_id;
}

esp_err_t thread_scheduler_init(void)
{
    ESP_LOGI(TAG, "Initializing global thread scheduler");
    
    // Initialize media library adapter
    media_lib_add_default_adapter();
    
    // Set the capture thread scheduler
    esp_capture_set_thread_scheduler(capture_scheduler);
    
    // Set the global thread scheduler callback
    media_lib_thread_set_schedule_cb(global_thread_scheduler);
    
    ESP_LOGI(TAG, "Thread scheduler initialized successfully");
    ESP_LOGI(TAG, "Stack allocations: WebRTC(35KB), Audio(40KB), Default(4KB)");
    
    return ESP_OK;
}