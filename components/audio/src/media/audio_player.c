#include "audio_player.h"
#include <esp_log.h>
#include "codec_init.h"
#include "codec_board.h"
#include "esp_codec_dev.h"
#include "av_render_default.h"
#include "esp_audio_dec_default.h"
#include "sdkconfig.h"
#include "audio_capture.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "memory_manager.h"
#include "recorder_module.h"

static const char *TAG = "audio_player";

esp_err_t audio_player_build_system(audio_player_system_t *player_sys, void *recorder_handle)
{
    if (!player_sys) {
        ESP_LOGE(TAG, "Invalid player system pointer");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Building audio player system");
    
    // Set recorder handle if provided
    player_sys->recorder_handle = recorder_handle;
    
    i2s_render_cfg_t i2s_cfg = {
        .play_handle = get_playback_handle(),
        .cb = recorder_handle ? recorder_audio_callback : NULL,
        .ctx = recorder_handle,
    };
    player_sys->audio_render = av_render_alloc_i2s_render(&i2s_cfg);
    if (player_sys->audio_render == NULL) {
        ESP_LOGE(TAG, "Failed to create audio render");
        return ESP_FAIL;
    }
    
    esp_codec_dev_set_out_vol(i2s_cfg.play_handle, CONFIG_AG_AUDIO_DEFAULT_PLAYBACK_VOL);
    
    av_render_cfg_t render_cfg = {
        .audio_render = player_sys->audio_render,
        .audio_raw_fifo_size = 8 * 4096,
        .audio_render_fifo_size = 100 * 1024,
        .allow_drop_data = false,
    };
    player_sys->player = av_render_open(&render_cfg);
    if (player_sys->player == NULL) {
        ESP_LOGE(TAG, "Failed to create player");
        return ESP_FAIL;
    }
    
    // Configure for WebRTC: 1 channel to match WebRTC OPUS configuration
    av_render_audio_frame_info_t aud_info = {
        .sample_rate = 24000,
        .channel = 2,
        .bits_per_sample = 16,
    };
    av_render_set_fixed_frame_info(player_sys->player, &aud_info);
    
    ESP_LOGI(TAG, "Audio player system built successfully");
    return ESP_OK;
}

esp_err_t audio_player_setup_loopback_test(audio_player_system_t *player_sys)
{
    if (!player_sys) {
        ESP_LOGE(TAG, "Invalid player system pointer");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Setting up player for loopback test");
    
    // Create player stream for test  
    av_render_audio_info_t render_aud_info = {
        .codec = AV_RENDER_AUDIO_CODEC_PCM,
        .sample_rate = 24000,
        .channel = 2,
    };
    
    int ret = av_render_add_audio_stream(player_sys->player, &render_aud_info);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to add audio stream: %d", ret);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Player setup for loopback test completed");
    return ESP_OK;
}

esp_err_t audio_player_reset(audio_player_system_t *player_sys)
{
    if (!player_sys) {
        ESP_LOGE(TAG, "Invalid player system pointer");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Resetting audio player");
    
    int ret = av_render_reset(player_sys->player);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to reset player: %d", ret);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Audio player reset completed");
    return ESP_OK;
}

// WAV Header structures
typedef struct {
    char     riff[4];          // "RIFF"
    uint32_t file_size;        // File size - 8
    char     wave[4];          // "WAVE"
} __attribute__((packed)) wav_riff_header_t;

typedef struct {
    char     fmt[4];           // "fmt "
    uint32_t fmt_size;         // Format chunk size
    uint16_t audio_format;     // Audio format (1 = PCM)
    uint16_t num_channels;     // Number of channels
    uint32_t sample_rate;      // Sample rate
    uint32_t byte_rate;        // Byte rate
    uint16_t block_align;      // Block align
    uint16_t bits_per_sample;  // Bits per sample
} __attribute__((packed)) wav_fmt_chunk_t;

typedef struct {
    char     data[4];          // "data"
    uint32_t data_size;        // Data size
} __attribute__((packed)) wav_data_chunk_t;

esp_err_t audio_player_play_wav(audio_player_system_t *player_sys, const char *filename)
{
    if (!player_sys || !player_sys->player || !filename) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "🔊 Playing WAV file: %s", filename);
    
    // Open file
    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open WAV file: %s", filename);
        return ESP_ERR_NOT_FOUND;
    }
    
    // Read RIFF header
    wav_riff_header_t riff_header;
    if (fread(&riff_header, 1, sizeof(riff_header), f) != sizeof(riff_header)) {
        ESP_LOGE(TAG, "Failed to read RIFF header");
        fclose(f);
        return ESP_FAIL;
    }
    
    // Validate RIFF format
    if (strncmp(riff_header.riff, "RIFF", 4) != 0 || strncmp(riff_header.wave, "WAVE", 4) != 0) {
        ESP_LOGE(TAG, "Invalid WAV file format");
        fclose(f);
        return ESP_FAIL;
    }
    
    // Find fmt and data chunks
    wav_fmt_chunk_t fmt_chunk = {0};
    wav_data_chunk_t data_chunk = {0};
    bool fmt_found = false, data_found = false;
    long data_start_pos = 0;
    
    // Try direct parsing first (fmt at offset 12)
    fseek(f, 12, SEEK_SET);
    char chunk_id[4];
    uint32_t wav_chunk_size;
    
    if (fread(chunk_id, 1, 4, f) == 4 && strncmp(chunk_id, "fmt ", 4) == 0) {
        if (fread(&wav_chunk_size, 1, 4, f) == 4 && wav_chunk_size >= 16) {
            if (fread(&fmt_chunk.audio_format, 1, 16, f) == 16) {
                memcpy(fmt_chunk.fmt, "fmt ", 4);
                fmt_chunk.fmt_size = wav_chunk_size;
                fmt_found = true;
                
                // Skip extra fmt data
                if (wav_chunk_size > 16) {
                    fseek(f, wav_chunk_size - 16, SEEK_CUR);
                }
                
                // Look for data chunk
                if (fread(chunk_id, 1, 4, f) == 4 && strncmp(chunk_id, "data", 4) == 0) {
                    if (fread(&wav_chunk_size, 1, 4, f) == 4) {
                        memcpy(data_chunk.data, "data", 4);
                        data_chunk.data_size = wav_chunk_size;
                        data_start_pos = ftell(f);
                        data_found = true;
                    }
                }
            }
        }
    }
    
    if (!fmt_found || !data_found) {
        ESP_LOGE(TAG, "Failed to parse WAV chunks");
        fclose(f);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "WAV: %"PRIu32"Hz, %d channels, %d bits, %"PRIu32" bytes", 
                fmt_chunk.sample_rate, fmt_chunk.num_channels, 
                fmt_chunk.bits_per_sample, data_chunk.data_size);
    
    // Add WAV audio stream using PCM codec (WAV files contain raw PCM data)
    av_render_audio_info_t wav_info = {
        .codec = AV_RENDER_AUDIO_CODEC_PCM,
        .sample_rate = fmt_chunk.sample_rate,
        .channel = fmt_chunk.num_channels,
        .bits_per_sample = fmt_chunk.bits_per_sample,
    };
    int ret = av_render_add_audio_stream(player_sys->player, &wav_info);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to add audio stream: %d", ret);
        fclose(f);
        return ESP_FAIL;
    }
    
    // Stream audio data directly from file (memory efficient)
    fseek(f, data_start_pos, SEEK_SET);
    
    // Stream audio with timing (memory efficient - read chunks as needed)
    uint32_t bytes_per_second = fmt_chunk.sample_rate * fmt_chunk.num_channels * (fmt_chunk.bits_per_sample / 8);
    const size_t chunk_size = (bytes_per_second * 20) / 1000; // 20ms chunks
    uint32_t bytes_sent = 0;
    uint32_t pts = 0;
    
    // Allocate small buffer for streaming chunks
    uint8_t *chunk_buffer = mem_alloc(chunk_size, MEM_POLICY_REQUIRE_INTERNAL, "audio_chunk");
    if (!chunk_buffer) {
        ESP_LOGE(TAG, "Failed to allocate chunk buffer (%zu bytes)", chunk_size);
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    
    while (bytes_sent < data_chunk.data_size) {
        size_t remaining = data_chunk.data_size - bytes_sent;
        size_t current_chunk = (remaining > chunk_size) ? chunk_size : remaining;
        
        // Read chunk from file
        size_t bytes_read = fread(chunk_buffer, 1, current_chunk, f);
        if (bytes_read == 0) {
            // End of file reached
            break;
        }
        if (bytes_read != current_chunk) {
            // Last chunk might be smaller, that's normal
            ESP_LOGI(TAG, "Last chunk: %zu bytes (expected %zu)", bytes_read, current_chunk);
            current_chunk = bytes_read; // Adjust to actual bytes read
        }
        
        av_render_audio_data_t audio_data = {
            .pts = pts,
            .data = chunk_buffer,
            .size = bytes_read, // Use actual bytes read
            .eos = false,
        };
        
        // Add audio data with retry
        int retry_count = 0;
        while ((ret = av_render_add_audio_data(player_sys->player, &audio_data)) != 0 && retry_count < 50) {
            vTaskDelay(pdMS_TO_TICKS(1));
            retry_count++;
        }
        
        if (ret != 0) {
            ESP_LOGW(TAG, "Failed to add audio data");
            break;
        }
        
        bytes_sent += bytes_read;
        pts += (bytes_read * 1000) / bytes_per_second;
        
        vTaskDelay(pdMS_TO_TICKS(20)); // 20ms delay
    }
    
    mem_free(chunk_buffer);
    
    // Send EOS
    av_render_audio_data_t eos_data = { .eos = true };
    av_render_add_audio_data(player_sys->player, &eos_data);
    
    // Close file after streaming
    fclose(f);
    
    av_render_flush(player_sys->player);
    
    // Reset player after WAV playback - WebRTC will restore OPUS stream when it resumes
    ret = av_render_reset(player_sys->player);
    if (ret != 0) {
        ESP_LOGE(TAG, "❌ Failed to reset player: %d", ret);
    } else {
        ESP_LOGI(TAG, "✅ Player reset - WebRTC will restore OPUS stream on resume");
    }
    
    ESP_LOGI(TAG, "✅ WAV playback completed: %s", filename);
    return ESP_OK;
}

esp_err_t audio_player_start_recording(audio_player_system_t *player_sys)
{
    if (!player_sys) {
        ESP_LOGE(TAG, "Invalid player system pointer");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!player_sys->recorder_handle) {
        ESP_LOGE(TAG, "Recorder not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = recorder_start(player_sys->recorder_handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "🔴 Recording started");
    } else {
        ESP_LOGE(TAG, "Failed to start recording: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

esp_err_t audio_player_stop_recording(audio_player_system_t *player_sys)
{
    if (!player_sys) {
        ESP_LOGE(TAG, "Invalid player system pointer");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!player_sys->recorder_handle) {
        ESP_LOGE(TAG, "Recorder not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = recorder_stop(player_sys->recorder_handle);
    if (ret == ESP_OK) {
        const char *filename = recorder_get_current_filename(player_sys->recorder_handle);
        size_t bytes = recorder_get_bytes_written(player_sys->recorder_handle);
        ESP_LOGI(TAG, "⏹️ Recording stopped: %s (%.2f MB)", 
                 filename, bytes / (1024.0 * 1024.0));
    } else {
        ESP_LOGE(TAG, "Failed to stop recording: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

bool audio_player_is_recording(audio_player_system_t *player_sys)
{
    if (!player_sys || !player_sys->recorder_handle) {
        return false;
    }
    
    recorder_state_t state = recorder_get_state(player_sys->recorder_handle);
    return (state == RECORDER_STATE_RECORDING);
}