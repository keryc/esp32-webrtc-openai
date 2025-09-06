/*
 * OpenAI Real-Time Chat - Minimal Refactored Main
 * 
 * Copyright (c) 2024 ESP-WebRTC Solution
 * Licensed under MIT License
 */

#include <stdio.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include "media_lib_os.h"
#include <freertos/task.h>
#include "console_module.h"
#include "wifi_module.h"
#include "wifi_commands.h"
#include "board_module.h"
#include "memory_manager.h"
#include "audio_module.h"
#include "audio_commands.h"
#include "audio_feedback.h"
#include "webrtc_module.h"
#include "webrtc_commands.h"
#include "camera_module.h"
#include "camera_commands.h"
#include "thread_scheduler.h"
#include "system_commands.h"
#include "sdspi_module.h"
#include "sdspi_commands.h"
#include "recorder_module.h"
#include "recorder_commands.h"
#include "openai_client.h"

static const char *TAG = "main";

static esp_err_t init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

// Async task functions with proper stack allocation
void webrtc_start_task(void *arg)
{
    ESP_LOGI(TAG, "Starting WebRTC...");
    webrtc_module_start();
    ESP_LOGI(TAG, "WebRTC started successfully");
    media_lib_thread_destroy(NULL);
}

void webrtc_stop_task(void *arg)
{
    ESP_LOGI(TAG, "Stopping WebRTC...");
    webrtc_module_stop();
    ESP_LOGI(TAG, "WebRTC stopped successfully");
    media_lib_thread_destroy(NULL);
}

// Event callbacks
static void wifi_event_callback(bool connected)
{
    if (connected) {
        ESP_LOGI(TAG, "WiFi connected");

        // Start audio module
        ESP_LOGI(TAG, "Starting audio module...");
        audio_module_start();

        // Set OpenAI activation mode to audio+vision
        openai_realtime_set_activation_mode(true);

        // Play pre-recorded startup sound
        ESP_LOGI(TAG, "🎵 Playing starting.wav feedback sound");
        esp_err_t ret = audio_feedback_play_wav("/spiffs/sounds/starting.wav", NULL);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to play starting.wav: %s", esp_err_to_name(ret));
        }

        // Images are sent directly through WebRTC data channel
        ESP_LOGI(TAG, "OpenAI activation mode set to audio+vision");

        media_lib_thread_create_from_scheduler(NULL, "webrtc_start", webrtc_start_task, NULL);
    } else {
        ESP_LOGI(TAG, "WiFi disconnected");
    }
}

static void webrtc_event_callback(webrtc_state_t state)
{  
    const char *state_str[] = {"DISCONNECTED", "CONNECTING", "CONNECTED", "FAILED"};
    ESP_LOGI(TAG, "WebRTC state changed to: %s", state_str[state]);
    
    if (state == WEBRTC_STATE_CONNECTED) {
        ESP_LOGI(TAG, "WebRTC connected");
    } else if (state == WEBRTC_STATE_FAILED) {
        ESP_LOGD(TAG, "WebRTC connection failed");
    } else if (state == WEBRTC_STATE_DISCONNECTED) {
        ESP_LOGD(TAG, "WebRTC disconnected unexpectedly");
    }
}

static void cam_event_callback(cam_event_t event, void *data)
{
    switch (event) {
        case CAM_EVENT_INITIALIZED:
            ESP_LOGI(TAG, "Camera/Vision module initialized");
            break;
        case CAM_EVENT_FRAME_READY:
            ESP_LOGI(TAG, "Camera/Vision frame ready");
            break;
        case CAM_EVENT_STREAM_STARTED:
            ESP_LOGI(TAG, "Camera/Vision stream started");

            // Update OpenAI activation mode when vision starts
            openai_realtime_set_activation_mode(true);

            ESP_LOGI(TAG, "OpenAI activation mode updated: vision enabled");
            break;
        case CAM_EVENT_STREAM_STOPPED:
            ESP_LOGI(TAG, "Camera/Vision stream stopped");

            // Update OpenAI activation mode when vision stops
            openai_realtime_set_activation_mode(false);

            ESP_LOGI(TAG, "OpenAI activation mode updated: vision disabled");
            break;
        case CAM_EVENT_ANALYSIS_COMPLETE:
            ESP_LOGI(TAG, "Camera/Vision analysis complete: %s", data ? (char*)data : "");
            break;
        case CAM_EVENT_ERROR:
            ESP_LOGI(TAG, "Camera/Vision error: %s", data ? (char*)data : "unknown");
            break;
    }
}

static void sdspi_event_callback(sdspi_event_t event, void *data)
{
    switch (event) {
        case SDSPI_EVENT_MOUNTED:
            ESP_LOGI(TAG, "SD card mounted successfully for audio recording");
            
            // Pequeño delay para asegurar que el filesystem esté listo
            vTaskDelay(pdMS_TO_TICKS(100));
            
            // Initialize recorder if SD card is mounted
            recorder_config_t rec_config = RECORDER_DEFAULT_CONFIG();
            rec_config.max_file_size_bytes = 0; // 0 = sin límite de tamaño
            recorder_handle_t rec_handle = NULL;
            esp_err_t rec_ret = recorder_init(&rec_config, &rec_handle);
            if (rec_ret == ESP_OK) {
                ESP_LOGI(TAG, "Audio recorder initialized");
                
                // Start auto-recording
                esp_err_t start_ret = recorder_start(rec_handle);
                if (start_ret == ESP_OK) {
                    ESP_LOGI(TAG, "🔴 Auto-recording started - capturing all audio to SD card");
                } else {
                    ESP_LOGE(TAG, "Failed to start auto-recording: %s", esp_err_to_name(start_ret));
                }
            } else {
                ESP_LOGW(TAG, "Failed to initialize recorder: %s", esp_err_to_name(rec_ret));
            }
            break;
            
        case SDSPI_EVENT_UNMOUNTED:
            ESP_LOGI(TAG, "SD card unmounted");
            break;
            
        case SDSPI_EVENT_ERROR:
            ESP_LOGE(TAG, "SD card error occurred");
            break;
            
        case SDSPI_EVENT_WRITE_COMPLETE:
            ESP_LOGD(TAG, "SD card write complete");
            break;
            
        case SDSPI_EVENT_READ_COMPLETE:
            ESP_LOGD(TAG, "SD card read complete");
            break;
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "===== Starting System =====");
    
    // Initialize global thread scheduler
    ESP_ERROR_CHECK(thread_scheduler_init());
    
    // Initialize memory manager for runtime detection and monitoring
    ESP_ERROR_CHECK(memory_manager_init());
    memory_manager_enable_monitoring(10000); // Monitor every 10 seconds for better visibility
    
    // Initialize board hardware peripherals (I2C, codec, camera interfaces, etc.)
    ESP_ERROR_CHECK(board_module_init());

    // Initialize SD card module before audio (for recording)
    ESP_LOGI(TAG, "Initializing SD card...");
    sdspi_config_t sd_config = SDSPI_DEFAULT_CONFIG();
    esp_err_t sd_ret = sdspi_module_init(&sd_config, sdspi_event_callback);
    if (sd_ret != ESP_OK) {
        ESP_LOGW(TAG, "SD card not available: %s", esp_err_to_name(sd_ret));
    }

    // Initialize audio module
    ESP_ERROR_CHECK(audio_module_init(NULL));

    // Audio Feedback module
    ESP_ERROR_CHECK(audio_feedback_init());
    
    // Initialize NVS (needed for WiFi)
    ESP_ERROR_CHECK(init_nvs());
    
    // Initialize console module
    ESP_ERROR_CHECK(console_module_init());
    
    // Initialize WiFi module
    ESP_ERROR_CHECK(wifi_module_init(wifi_event_callback));

    // Initialize WebRTC module
    ESP_ERROR_CHECK(webrtc_module_init(webrtc_event_callback));
    
    // Initialize unified camera/vision module with Kconfig settings
    cam_config_t cam_config = {
        .mode = CAM_MODE_ANALYSIS_ONLY, // AI analysis mode
        .quality = CONFIG_AG_VISION_DEFAULT_QUALITY,
        .fps = CONFIG_AG_VISION_DEFAULT_FPS,
        .auto_exposure = true, // Essential for varying light conditions
        .auto_white_balance = true, // Natural colors under different lighting
        .jpeg_quality = CONFIG_AG_VISION_JPEG_QUALITY,
        .buffer_frames = CONFIG_AG_VISION_BUFFER_FRAMES,
        .enable_live_preview = false // Disable HTTP preview by default (save CPU)
    };
    ESP_ERROR_CHECK(cam_module_init(&cam_config, cam_event_callback));
    
    // Register console commands
    ESP_ERROR_CHECK(console_register_commands());
    
    // Register module commands
    ESP_ERROR_CHECK(wifi_register_commands());
    ESP_ERROR_CHECK(audio_register_commands());
    ESP_ERROR_CHECK(webrtc_register_commands());
    ESP_ERROR_CHECK(camera_commands_register());
    ESP_ERROR_CHECK(system_commands_register());
    ESP_ERROR_CHECK(sdspi_commands_register());
    ESP_ERROR_CHECK(recorder_commands_register());
    
    // Start console task
    ESP_ERROR_CHECK(console_module_start());

    // Try to auto-connect if credentials are saved
    if (wifi_module_load_credentials() == ESP_OK) {
        wifi_credentials_t creds;
        wifi_module_get_credentials(&creds);
        ESP_LOGI(TAG, "Auto-connecting to saved network: %s", creds.ssid);
        wifi_module_connect(creds.ssid, creds.password);
    }
    
    while (1) {        
        // Reduced polling frequency to save power
        media_lib_thread_sleep(5000);
        // Query WebRTC status periodically if connected
        webrtc_module_query_status();
    }
}
