#include "audio_module.h"
#include <esp_log.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_capture.h"
#include "av_render.h"
#include "esp_webrtc.h"
#include "esp_codec_dev.h"
#include "media_lib_os.h"
#include "esp_timer.h"
#include "esp_audio_dec_default.h"
#include "esp_audio_enc_default.h"
#include "codec_init.h"
#include "media/audio_capture.h"
#include "media/audio_player.h"
#include "media/audio_media.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "audio_module";

#define RET_ON_NULL(ptr, v) do {                                \
    if (ptr == NULL) {                                          \
        ESP_LOGE(TAG, "Memory allocate fail on %d", __LINE__);  \
        return v;                                               \
    }                                                           \
} while (0)


// Module state
static struct {
    bool initialized;
    bool system_ready;
    audio_event_callback_t event_callback;
    int current_volume;
    bool output_released;
    audio_capture_system_t capture_sys;
    audio_player_system_t player_sys;
    esp_capture_sink_handle_t primary_capture_path;
} audio_state = {0};

// Internal media system builder using submodules
static esp_err_t audio_buildup_media_system(void)
{
    ESP_LOGI(TAG, "Building audio media system using submodules");
    
    // Register default encoders/decoders
    esp_audio_enc_register_default();
    esp_audio_dec_register_default();
    
    // Build capture system using submodule
    esp_err_t ret = audio_capture_build_system(&audio_state.capture_sys);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to build capture system: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Build player system using submodule
    ret = audio_player_build_system(&audio_state.player_sys);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to build player system: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Audio media system built successfully");
    return ESP_OK;
}

esp_err_t audio_module_init(audio_event_callback_t callback)
{
    if (audio_state.initialized) {
        ESP_LOGW(TAG, "Audio module already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing audio module");
    
    // Store callback
    audio_state.event_callback = callback;
    audio_state.current_volume = CONFIG_AG_AUDIO_DEFAULT_PLAYBACK_VOL;
    audio_state.initialized = true;
    
    ESP_LOGI(TAG, "Audio module initialized");
    return ESP_OK;
}

esp_err_t audio_module_start(void)
{
    if (!audio_state.initialized) {
        ESP_LOGE(TAG, "Audio module not initialized");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Starting audio system...");
    
    // Build up media system using submodules
    esp_err_t ret = audio_buildup_media_system();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to build media system: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Set initial volume
    esp_codec_dev_handle_t play_handle = get_playback_handle();
    if (play_handle) {
        esp_codec_dev_set_out_vol(play_handle, audio_state.current_volume);
        ESP_LOGI(TAG, "Set playback volume to %d", audio_state.current_volume);
    } else {
        ESP_LOGW(TAG, "No playback handle available - board may not be initialized");
    }
    
    // Set initial microphone gain using codec handles directly
    esp_codec_dev_handle_t record_handle = get_record_handle();
    if (record_handle) {
        esp_codec_dev_set_in_gain(record_handle, CONFIG_AG_AUDIO_DEFAULT_MIC_GAIN);
        ESP_LOGI(TAG, "Set microphone gain to %.1f", (float)CONFIG_AG_AUDIO_DEFAULT_MIC_GAIN);
    } else {
        ESP_LOGW(TAG, "No record handle available - board may not be initialized");
    }
    
    audio_state.system_ready = true;
    
    // Notify via callback
    if (audio_state.event_callback) {
        audio_state.event_callback(true);
    }
    
    ESP_LOGI(TAG, "Audio system started successfully");
    return ESP_OK;
}

esp_err_t audio_module_stop(void)
{
    if (!audio_state.initialized) {
        ESP_LOGE(TAG, "Audio module not initialized");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Stopping audio system");
    
    audio_state.system_ready = false;
    
    // Notify via callback
    if (audio_state.event_callback) {
        audio_state.event_callback(false);
    }
    
    ESP_LOGI(TAG, "Audio system stopped");
    return ESP_OK;
}

bool audio_module_is_ready(void)
{
    return audio_state.system_ready;
}

int audio_module_get_volume(void)
{
    return audio_state.current_volume;
}

esp_err_t audio_module_set_volume(int volume)
{
    if (!audio_state.initialized) {
        ESP_LOGE(TAG, "Audio module not initialized");
        return ESP_FAIL;
    }
    
    if (volume < 0 || volume > 100) {
        ESP_LOGE(TAG, "Invalid volume level: %d", volume);
        return ESP_ERR_INVALID_ARG;
    }
    
    audio_state.current_volume = volume;
    
    // Apply volume if system is ready
    if (audio_state.system_ready) {
        esp_codec_dev_handle_t play_handle = get_playback_handle();
        if (play_handle) {
            esp_err_t ret = esp_codec_dev_set_out_vol(play_handle, volume);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set volume: %s", esp_err_to_name(ret));
                return ret;
            }
        }
    }
    
    ESP_LOGI(TAG, "Volume set to %d", volume);
    return ESP_OK;
}

esp_err_t audio_module_set_mic_gain(float gain)
{
    if (!audio_state.initialized) {
        ESP_LOGE(TAG, "Audio module not initialized");
        return ESP_FAIL;
    }
    
    if (gain < 0.0 || gain > 100.0) {
        ESP_LOGE(TAG, "Invalid mic gain level: %.1f", gain);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Apply gain if system is ready
    if (audio_state.system_ready) {
        esp_codec_dev_handle_t record_handle = get_record_handle();
        if (record_handle) {
            esp_err_t ret = esp_codec_dev_set_in_gain(record_handle, gain);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set mic gain: %s", esp_err_to_name(ret));
                return ret;
            }
        }
    }
    
    ESP_LOGI(TAG, "Mic gain set to %.1f", gain);
    return ESP_OK;
}

esp_err_t audio_module_test_loopback(void)
{
    if (!audio_state.system_ready) {
        ESP_LOGE(TAG, "Audio system not ready");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Starting audio loopback test using submodules");
    
    // Start capture using submodule
    esp_err_t ret = audio_capture_start_loopback_test(&audio_state.capture_sys, &audio_state.primary_capture_path);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start capture loopback test: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Setup player using submodule
    ret = audio_player_setup_loopback_test(&audio_state.player_sys);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to setup player loopback test: %s", esp_err_to_name(ret));
        audio_capture_stop_loopback_test(&audio_state.capture_sys);
        return ret;
    }

    // Run loopback test for 20 seconds
    uint32_t start_time = (uint32_t)(esp_timer_get_time() / 1000);
    while ((uint32_t)(esp_timer_get_time() / 1000) < start_time + 20000) {
        media_lib_thread_sleep(30);
        esp_capture_stream_frame_t frame = {
            .stream_type = ESP_CAPTURE_STREAM_TYPE_AUDIO,
        };
        while (esp_capture_sink_acquire_frame(audio_state.primary_capture_path, &frame, true) == ESP_CAPTURE_ERR_OK) {
            av_render_audio_data_t audio_data = {
                .data = frame.data,
                .size = frame.size,
                .pts = frame.pts,
            };
            av_render_add_audio_data(audio_state.player_sys.player, &audio_data);
            esp_capture_sink_release_frame(audio_state.primary_capture_path, &frame);
        }
    }
    
    // Stop capture and reset player using submodules
    audio_capture_stop_loopback_test(&audio_state.capture_sys);
    audio_player_reset(&audio_state.player_sys);
    
    ESP_LOGI(TAG, "Audio loopback test completed successfully");
    return ESP_OK;
}

esp_err_t audio_module_get_media_provider(esp_webrtc_media_provider_t *provider)
{
    if (!provider) {
        ESP_LOGE(TAG, "Invalid provider pointer");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!audio_state.system_ready) {
        ESP_LOGE(TAG, "Audio system not ready");
        return ESP_FAIL;
    }
    
    provider->capture = audio_state.capture_sys.capture_handle;
    provider->player = audio_state.player_sys.player;
    
    return ESP_OK;
}

esp_err_t audio_module_release_output(void)
{
    if (!audio_state.initialized) {
        ESP_LOGE(TAG, "Audio module not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (audio_state.output_released) {
        ESP_LOGW(TAG, "Audio output already released");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Releasing audio output resources for feedback playback");
    
    // Pause the player to release I2S resources
    if (audio_state.system_ready && audio_state.player_sys.player) {
        int ret = av_render_pause(audio_state.player_sys.player, true);
        if (ret != 0) {
            ESP_LOGW(TAG, "Failed to pause player: %d", ret);
        }
    }
    
    audio_state.output_released = true;
    return ESP_OK;
}

esp_err_t audio_module_restore_output(void)
{
    if (!audio_state.initialized) {
        ESP_LOGE(TAG, "Audio module not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!audio_state.output_released) {
        ESP_LOGW(TAG, "Audio output not released");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Restoring audio output resources after feedback playback");
    
    // Resume the player to restore I2S resources
    if (audio_state.system_ready && audio_state.player_sys.player) {
        ESP_LOGI(TAG, "Calling av_render_pause(player, false) to resume player: %p", audio_state.player_sys.player);
        int ret = av_render_pause(audio_state.player_sys.player, false);
        if (ret != 0) {
            ESP_LOGE(TAG, "Failed to resume player: %d", ret);
            return ESP_FAIL;
        } else {
            ESP_LOGI(TAG, "Player resumed successfully");
        }
        
        // Small delay to ensure hardware is stable after resume
        vTaskDelay(pdMS_TO_TICKS(15));
        
        ESP_LOGI(TAG, "🔄 Restoring WebRTC OPUS stream configuration");
        
        av_render_audio_frame_info_t webrtc_format = {
            .sample_rate = 24000,
            .channel = 2,
            .bits_per_sample = 16,
        };
        av_render_set_fixed_frame_info(audio_state.player_sys.player, &webrtc_format);
        
        av_render_audio_info_t webrtc_stream = {
            .codec = AV_RENDER_AUDIO_CODEC_PCM,
            .sample_rate = 24000,
            .channel = 2,
        };
        ret = av_render_add_audio_stream(audio_state.player_sys.player, &webrtc_stream);
        if (ret != 0) {
            ESP_LOGE(TAG, "❌ CRITICAL: Failed to restore WebRTC OPUS stream: %d", ret);
            return ESP_FAIL;
        } else {
            ESP_LOGI(TAG, "✅ WebRTC OPUS stream restored successfully");
        }
        
    } else {
        ESP_LOGW(TAG, "Cannot resume - system_ready: %d, player: %p", 
                audio_state.system_ready, audio_state.player_sys.player);
    }
    
    audio_state.output_released = false;
    ESP_LOGI(TAG, "Audio output restoration completed");
    return ESP_OK;
}