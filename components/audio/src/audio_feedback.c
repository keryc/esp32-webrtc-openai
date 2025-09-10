#include "audio_feedback.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_spiffs.h"
#include "media/audio_player.h"
#include "media/audio_media.h"
#include "webrtc_module.h"
#include "memory_manager.h"
#include <inttypes.h>
#include <string.h>

static const char *TAG = "audio_feedback";

// Forward declarations
static void audio_feedback_playback_task(void *pvParameters);

// Module state
static struct {
    bool initialized;
    bool is_playing;
    audio_feedback_callback_t current_callback;
    audio_feedback_wav_callback_t current_wav_callback;
    char *current_filename;
    uint8_t volume;
    
    // Audio player system
    audio_player_system_t player_sys;
    
    // Async playback task
    TaskHandle_t playback_task_handle;
    
    // Optional recorder handle
    void *recorder_handle;
} feedback_state = {0};



esp_err_t audio_feedback_init(void)
{
    if (feedback_state.initialized) {
        ESP_LOGD(TAG, "Audio feedback already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing audio feedback system");
    
    // Initialize SPIFFS
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };
    
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        return ret;
    }
    
    // Initialize audio player system (will use recorder if set later)
    ret = audio_player_build_system(&feedback_state.player_sys, feedback_state.recorder_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize audio player");
        esp_vfs_spiffs_unregister(NULL);
        return ret;
    }
    
    
    feedback_state.initialized = true;
    feedback_state.is_playing = false;
    feedback_state.volume = 80; // Default volume
    
    ESP_LOGI(TAG, "Audio feedback system initialized successfully");
    return ESP_OK;
}

void audio_feedback_set_recorder_handle(void *recorder_handle)
{
    feedback_state.recorder_handle = recorder_handle;
    // Update the player system with the new recorder handle
    if (feedback_state.initialized) {
        feedback_state.player_sys.recorder_handle = recorder_handle;
        ESP_LOGI(TAG, "Audio feedback recorder handle set: %p", recorder_handle);
    }
}

// Asynchronous WAV playback task
static void audio_feedback_playback_task(void *pvParameters)
{
    char *filename = (char *)pvParameters;
    
    ESP_LOGI(TAG, "Async playback task started for: %s", filename);
    
    bool webrtc_was_active = webrtc_module_is_connected();
    if (webrtc_was_active) {
        ESP_LOGI(TAG, "WebRTC is active - ensuring audio is paused for feedback playback");
        webrtc_module_pause_audio(); // This will ensure audio resources are released
    }
    
    // Play the audio file
    esp_err_t ret = audio_player_play_wav(&feedback_state.player_sys, filename);
    
    // Always resume WebRTC audio if it was active (enables first-time activation)
    if (webrtc_was_active) {
        ESP_LOGI(TAG, "Resuming/enabling WebRTC audio after feedback playback");
        webrtc_module_resume_audio(); // This will enable audio for the first time if needed
    }
    
    // Update state and call callback
    feedback_state.is_playing = false;
    if (feedback_state.current_wav_callback) {
        feedback_state.current_wav_callback(filename, ret == ESP_OK);
    }
    
    // Clean up
    mem_free(filename);
    feedback_state.playback_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t audio_feedback_play_wav(const char *filename, audio_feedback_wav_callback_t callback)
{
    if (!feedback_state.initialized) {
        ESP_LOGE(TAG, "Audio feedback not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!filename) {
        ESP_LOGE(TAG, "Invalid filename");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (feedback_state.is_playing) {
        ESP_LOGW(TAG, "Audio feedback already playing, stopping current playback");
        audio_feedback_stop();
    }
    
    ESP_LOGI(TAG, "Starting async WAV playback: %s", filename);
    
    feedback_state.is_playing = true;
    feedback_state.current_wav_callback = callback;
    
    // Create a copy of the filename for the task
    char *filename_copy = mem_alloc(strlen(filename) + 1, MEM_POLICY_PREFER_PSRAM, "wav_filename");
    if (!filename_copy) {
        ESP_LOGE(TAG, "Failed to allocate memory for filename");
        feedback_state.is_playing = false;
        return ESP_ERR_NO_MEM;
    }
    strcpy(filename_copy, filename);
    
    // Create playback task pinned to Core 0 for audio processing
    BaseType_t ret = xTaskCreatePinnedToCore(
        audio_feedback_playback_task,
        "audio_feedback_task",
        4096,
        filename_copy,
        5,
        &feedback_state.playback_task_handle,
        0  // Pin to Core 0 for audio
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create playback task");
        mem_free(filename_copy);
        feedback_state.is_playing = false;
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

esp_err_t audio_feedback_stop(void)
{
    if (!feedback_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Stopping current feedback");
    
    feedback_state.is_playing = false;
    
    // Stop async task if running
    if (feedback_state.playback_task_handle) {
        vTaskDelete(feedback_state.playback_task_handle);
        feedback_state.playback_task_handle = NULL;
    }
    
    // Reset player to stop current playback
    audio_player_reset(&feedback_state.player_sys);
    
    return ESP_OK;
}

bool audio_feedback_is_playing(void)
{
    return feedback_state.is_playing;
}

esp_err_t audio_feedback_set_volume(uint8_t volume)
{
    if (!feedback_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (volume > 100) {
        volume = 100;
    }
    
    feedback_state.volume = volume;
    ESP_LOGI(TAG, "Volume set to %d%%", volume);
    
    return ESP_OK;
}


