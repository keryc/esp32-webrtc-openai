#include "webrtc_module.h"
#include <esp_log.h>
#include <string.h>
#include "common.h"
#include "wifi_module.h"
#include "providers/openai/openai_client.h"
static const char *TAG = "webrtc_module";

// Module state
static struct {
    bool initialized;
    webrtc_state_t current_state;
    webrtc_event_callback_t event_callback;
} webrtc_state = {0};

// State change helper
static void set_webrtc_state(webrtc_state_t new_state)
{
    if (webrtc_state.current_state != new_state) {
        webrtc_state.current_state = new_state;
        
        const char *state_str[] = {"DISCONNECTED", "CONNECTING", "CONNECTED", "FAILED"};
        ESP_LOGI(TAG, "WebRTC state changed to: %s", state_str[new_state]);
        
        // Notify via callback
        if (webrtc_state.event_callback) {
            webrtc_state.event_callback(new_state);
        }
    }
}

esp_err_t webrtc_module_init(webrtc_event_callback_t callback)
{
    if (webrtc_state.initialized) {
        ESP_LOGW(TAG, "WebRTC module already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing WebRTC module");
    
    // Store callback
    webrtc_state.event_callback = callback;
    webrtc_state.current_state = WEBRTC_STATE_DISCONNECTED;
    webrtc_state.initialized = true;
    
    ESP_LOGI(TAG, "WebRTC module initialized");
    return ESP_OK;
}

esp_err_t webrtc_module_start(void)
{
    if (!webrtc_state.initialized) {
        ESP_LOGE(TAG, "WebRTC module not initialized");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Starting WebRTC session");
    set_webrtc_state(WEBRTC_STATE_CONNECTING);
    
    // Validate WiFi connection before starting WebRTC
    if (!wifi_module_is_connected()) {
        ESP_LOGE(TAG, "WiFi not connected. Use wifi command to connect first.");
        set_webrtc_state(WEBRTC_STATE_FAILED);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "WiFi connection verified, starting WebRTC");
    esp_err_t ret = openai_realtime_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start OpenAI WebRTC: %s", esp_err_to_name(ret));
        set_webrtc_state(WEBRTC_STATE_FAILED);
        return ret;
    }
    
    set_webrtc_state(WEBRTC_STATE_CONNECTED);
    ESP_LOGI(TAG, "WebRTC session started successfully");
    return ESP_OK;
}

esp_err_t webrtc_module_stop(void)
{
    if (!webrtc_state.initialized) {
        ESP_LOGE(TAG, "WebRTC module not initialized");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Stopping WebRTC session");
    
    esp_err_t ret = openai_realtime_stop();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop OpenAI WebRTC: %s", esp_err_to_name(ret));
        return ret;
    }
    
    set_webrtc_state(WEBRTC_STATE_DISCONNECTED);
    ESP_LOGI(TAG, "WebRTC session stopped");
    return ESP_OK;
}

webrtc_state_t webrtc_module_get_state(void)
{
    return webrtc_state.current_state;
}

esp_err_t webrtc_module_send_text(const char *text)
{
    if (!webrtc_state.initialized) {
        ESP_LOGE(TAG, "WebRTC module not initialized");
        return ESP_FAIL;
    }
    
    if (!text || strlen(text) == 0) {
        ESP_LOGE(TAG, "Invalid text message");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (webrtc_state.current_state != WEBRTC_STATE_CONNECTED) {
        ESP_LOGE(TAG, "WebRTC not connected");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Sending text to OpenAI: %s", text);
    
    esp_err_t ret = openai_realtime_send_text(text);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send text: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Text sent successfully");
    return ESP_OK;
}

esp_err_t webrtc_module_query_status(void)
{
    if (!webrtc_state.initialized) {
        ESP_LOGE(TAG, "WebRTC module not initialized");
        return ESP_FAIL;
    }
    
    return openai_realtime_query();
}

bool webrtc_module_is_connected(void)
{
    return webrtc_state.current_state == WEBRTC_STATE_CONNECTED && openai_realtime_is_connected();
}

esp_err_t webrtc_module_pause_audio(void)
{
    if (!webrtc_state.initialized) {
        ESP_LOGE(TAG, "WebRTC module not initialized");
        return ESP_FAIL;
    }
    
    if (webrtc_state.current_state != WEBRTC_STATE_CONNECTED) {
        ESP_LOGW(TAG, "WebRTC not connected, cannot pause audio");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Pausing WebRTC audio");
    
    // Delegate to OpenAI client
    extern esp_err_t openai_realtime_pause_audio(void);
    return openai_realtime_pause_audio();
}

esp_err_t webrtc_module_resume_audio(void)
{
    if (!webrtc_state.initialized) {
        ESP_LOGE(TAG, "WebRTC module not initialized");
        return ESP_FAIL;
    }
    
    if (webrtc_state.current_state != WEBRTC_STATE_CONNECTED) {
        ESP_LOGW(TAG, "WebRTC not connected, cannot resume audio");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Resuming WebRTC audio");
    
    // Delegate to OpenAI client
    extern esp_err_t openai_realtime_resume_audio(void);
    return openai_realtime_resume_audio();
}