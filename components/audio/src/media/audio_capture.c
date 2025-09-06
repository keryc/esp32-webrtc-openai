#include "audio_capture.h"
#include <esp_log.h>
#include "codec_init.h"
#include "codec_board.h"
#include "esp_capture.h"
#include "esp_capture_sink.h"
#include "esp_capture_audio_aec_src.h"
#include "esp_audio_enc_default.h"
#include "esp_capture_defaults.h"
#include "sdkconfig.h"

static const char *TAG = "audio_capture";

#define RET_ON_NULL(ptr, v) do {                                \
    if (ptr == NULL) {                                          \
        ESP_LOGE(TAG, "Memory allocate fail on %d", __LINE__);  \
        return v;                                               \
    }                                                           \
} while (0)

esp_err_t audio_capture_build_system(audio_capture_system_t *capture_sys)
{
    if (!capture_sys) {
        ESP_LOGE(TAG, "Invalid capture system pointer");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Building audio capture system");
    
    ESP_LOGI(TAG, "Configuring for I2S digital microphone");
    ESP_LOGI(TAG, "I2S microphone settings - Sample rate: %d Hz, Bit depth: %d, Channels: %d", 
             CONFIG_AG_AUDIO_MIC_SAMPLE_RATE, CONFIG_AG_AUDIO_MIC_BIT_DEPTH, CONFIG_AG_AUDIO_MIC_CHANNELS);
#ifdef CONFIG_AG_AUDIO_MIC_EXTENDED_BIT_DEPTH
    ESP_LOGI(TAG, "Note: Microphone supports up to %d-bit but using %d-bit for codec compatibility", 
             CONFIG_AG_AUDIO_MIC_EXTENDED_BIT_DEPTH, CONFIG_AG_AUDIO_MIC_BIT_DEPTH);
#endif
    
    // AEC (Acoustic Echo Cancellation) controlled by Kconfig
#ifdef CONFIG_AG_AUDIO_ENABLE_AEC
    // Use AEC audio source for echo cancellation
    esp_capture_audio_aec_src_cfg_t codec_cfg = {
        .record_handle = get_record_handle(),
    #if CONFIG_IDF_TARGET_ESP32S3
            .channel = 4,
            .channel_mask = 1 | 2,
    #endif
    };
    
    ESP_LOGI(TAG, "✅ Using AEC audio source (echo cancellation enabled)");
    ESP_LOGW(TAG, "⚠️ WARNING: AEC may cause memzero_int16_728 crashes on some configs");
    capture_sys->aud_src = esp_capture_new_audio_aec_src(&codec_cfg);
#else
    // Use basic audio source without AEC (more stable)
    esp_capture_audio_dev_src_cfg_t codec_cfg = {
        .record_handle = get_record_handle(),
    };
    
    ESP_LOGI(TAG, "Using basic audio source (AEC disabled for stability)");
    ESP_LOGI(TAG, "To enable AEC: set CONFIG_AG_AUDIO_ENABLE_AEC=y in menuconfig");
    capture_sys->aud_src = esp_capture_new_audio_dev_src(&codec_cfg);
#endif
    
    RET_ON_NULL(capture_sys->aud_src, ESP_ERR_NO_MEM);
    ESP_LOGI(TAG, "Audio source created successfully");
    
    // Create capture system
    esp_capture_cfg_t cfg = {
        .sync_mode = ESP_CAPTURE_SYNC_MODE_AUDIO,
        .audio_src = capture_sys->aud_src,
    };
    
    int ret = esp_capture_open(&cfg, &capture_sys->capture_handle);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to open capture system: %d", ret);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Audio capture system built successfully");
    return ESP_OK;
}

esp_err_t audio_capture_start_loopback_test(audio_capture_system_t *capture_sys, 
                                           esp_capture_sink_handle_t *primary_path_out)
{
    if (!capture_sys || !primary_path_out) {
        ESP_LOGE(TAG, "Invalid parameters for loopback test");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Starting capture loopback test");
    
    esp_capture_sink_cfg_t sink_cfg = {
        .audio_info = {
            .format_id = ESP_CAPTURE_FMT_ID_OPUS,
            .sample_rate = CONFIG_AG_AUDIO_MIC_SAMPLE_RATE,
            .channel = CONFIG_AG_AUDIO_MIC_CHANNELS,
            .bits_per_sample = CONFIG_AG_AUDIO_MIC_BIT_DEPTH,
        },
    };
    
    // Create capture sink
    esp_capture_sink_handle_t capture_path = NULL;
    int ret = esp_capture_sink_setup(capture_sys->capture_handle, 0, &sink_cfg, &capture_path);
    if (ret != ESP_CAPTURE_ERR_OK) {
        ESP_LOGE(TAG, "Failed to setup capture sink: %d", ret);
        return ESP_FAIL;
    }
    
    ret = esp_capture_sink_enable(capture_path, ESP_CAPTURE_RUN_MODE_ALWAYS);
    if (ret != ESP_CAPTURE_ERR_OK) {
        ESP_LOGE(TAG, "Failed to enable capture sink: %d", ret);
        return ESP_FAIL;
    }
    
    ret = esp_capture_start(capture_sys->capture_handle);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to start capture: %d", ret);
        return ESP_FAIL;
    }
    
    *primary_path_out = capture_path;
    ESP_LOGI(TAG, "Capture loopback test started");
    return ESP_OK;
}

esp_err_t audio_capture_stop_loopback_test(audio_capture_system_t *capture_sys)
{
    if (!capture_sys) {
        ESP_LOGE(TAG, "Invalid capture system pointer");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Stopping capture loopback test");
    
    int ret = esp_capture_stop(capture_sys->capture_handle);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to stop capture: %d", ret);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Capture loopback test stopped");
    return ESP_OK;
}

esp_err_t audio_capture_set_mic_gain(float gain_percent)
{
    if (gain_percent < 0.0f || gain_percent > 100.0f) {
        ESP_LOGE(TAG, "Invalid gain percentage: %.1f (must be 0-100)", gain_percent);
        return ESP_ERR_INVALID_ARG;
    }

    // For I2S microphones, gain is typically handled in software/codec
    ESP_LOGI(TAG, "I2S microphone gain set to %.1f%% (software controlled)", gain_percent);
    // TODO: Implement software gain control if needed
    return ESP_OK;
}

esp_err_t audio_capture_get_optimal_settings(uint32_t *sample_rate, uint8_t *bit_depth, uint8_t *channels)
{
    if (!sample_rate || !bit_depth || !channels) {
        return ESP_ERR_INVALID_ARG;
    }

    *sample_rate = CONFIG_AG_AUDIO_MIC_SAMPLE_RATE;
    *bit_depth = CONFIG_AG_AUDIO_MIC_BIT_DEPTH;
    *channels = CONFIG_AG_AUDIO_MIC_CHANNELS;
    
    ESP_LOGI(TAG, "I2S microphone optimal settings: %lu Hz, %d-bit, %d channel(s)", 
             *sample_rate, *bit_depth, *channels);

    return ESP_OK;
}

esp_err_t audio_capture_get_codec_optimal_settings(const char *codec_name, uint32_t *sample_rate, uint8_t *bit_depth, uint8_t *channels)
{
    if (!codec_name || !sample_rate || !bit_depth || !channels) {
        return ESP_ERR_INVALID_ARG;
    }

    // Set base settings from microphone
    esp_err_t ret = audio_capture_get_optimal_settings(sample_rate, bit_depth, channels);
    if (ret != ESP_OK) {
        return ret;
    }

    // Adjust for codec limitations
    if (strcmp(codec_name, "OPUS") == 0) {
        // OPUS limitations
        if (*bit_depth > 16) {
            ESP_LOGI(TAG, "OPUS only supports 16-bit, adjusting from %d-bit", *bit_depth);
            *bit_depth = 16;
        }
        // OPUS supports 8kHz, 12kHz, 16kHz, 24kHz, 48kHz
        if (*sample_rate != 8000 && *sample_rate != 12000 && *sample_rate != 16000 && 
            *sample_rate != 24000 && *sample_rate != 48000) {
            ESP_LOGI(TAG, "OPUS prefers standard rates, keeping %lu Hz", *sample_rate);
        }
        ESP_LOGI(TAG, "OPUS optimal settings: %lu Hz, %d-bit, %d channel(s)", 
                 *sample_rate, *bit_depth, *channels);
                 
    } else if (strcmp(codec_name, "PCM") == 0) {
        // PCM can handle full resolution
#ifdef CONFIG_AG_AUDIO_MIC_EXTENDED_BIT_DEPTH
        *bit_depth = CONFIG_AG_AUDIO_MIC_EXTENDED_BIT_DEPTH;  // Use full extended bit depth for PCM
#endif
        ESP_LOGI(TAG, "PCM optimal settings: %lu Hz, %d-bit, %d channel(s)", 
                 *sample_rate, *bit_depth, *channels);
                 
    } else if (strcmp(codec_name, "AAC") == 0) {
        // AAC typically supports up to 24-bit
        ESP_LOGI(TAG, "AAC optimal settings: %lu Hz, %d-bit, %d channel(s)", 
                 *sample_rate, *bit_depth, *channels);
    } else {
        ESP_LOGI(TAG, "Unknown codec '%s', using default settings", codec_name);
    }

    return ESP_OK;
}