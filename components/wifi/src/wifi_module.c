#include "wifi_module.h"
#include <string.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <nvs.h>
#include <nvs_flash.h>
#include "sdkconfig.h"
static const char *TAG = "wifi_module";

// NVS namespace and keys
#define NVS_NAMESPACE "wifi_creds"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASS "password"

// Module state
static struct {
    bool initialized;
    bool connected;
    wifi_event_callback_t event_callback;
    wifi_config_t wifi_config;
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
} wifi_state = {0};

// Event handler
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi started, connecting...");
                esp_wifi_connect();
                break;
                
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI(TAG, "WiFi disconnected, retrying...");
                wifi_state.connected = false;
                if (wifi_state.event_callback) {
                    wifi_state.event_callback(false);
                }
                esp_wifi_connect();
                break;
                
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "WiFi connected to AP");
                break;
                
            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        wifi_state.connected = true;
        if (wifi_state.event_callback) {
            wifi_state.event_callback(true);
        }
        // Auto-save credentials on successful connection
        wifi_module_save_credentials();
    }
}

esp_err_t wifi_module_init(wifi_event_callback_t callback)
{
    if (wifi_state.initialized) {
        ESP_LOGW(TAG, "WiFi module already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing WiFi module");
    
    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());
    
    // Create default event loop if not already created
    esp_err_t ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to create event loop: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Create default WiFi station
    esp_netif_create_default_wifi_sta();
    
    // Initialize WiFi with default config
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &wifi_event_handler,
        NULL,
        &wifi_state.instance_any_id));
    
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &wifi_event_handler,
        NULL,
        &wifi_state.instance_got_ip));
    
    // Set WiFi mode to station
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    
    // Disable power save for better performance
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    
    // Store callback
    wifi_state.event_callback = callback;
    wifi_state.initialized = true;
    
    ESP_LOGI(TAG, "WiFi module initialized");
    return ESP_OK;
}

esp_err_t wifi_module_connect(const char *ssid, const char *password)
{
    if (!wifi_state.initialized) {
        ESP_LOGE(TAG, "WiFi module not initialized");
        return ESP_FAIL;
    }
    
    if (!ssid || strlen(ssid) == 0) {
        ESP_LOGE(TAG, "Invalid SSID");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Connecting to SSID: %s", ssid);
    
    // Clear current config
    memset(&wifi_state.wifi_config, 0, sizeof(wifi_config_t));
    
    // Set SSID
    strlcpy((char *)wifi_state.wifi_config.sta.ssid, ssid, 
            sizeof(wifi_state.wifi_config.sta.ssid));
    
    // Set password if provided
    if (password && strlen(password) > 0) {
        strlcpy((char *)wifi_state.wifi_config.sta.password, password,
                sizeof(wifi_state.wifi_config.sta.password));
    }
    
    // Set security threshold
    wifi_state.wifi_config.sta.threshold.authmode = 
        (password && strlen(password) > 0) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    
    // Stop WiFi if running
    esp_wifi_stop();
    
    // Apply new config
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_state.wifi_config));
    
    // Start WiFi
    ESP_ERROR_CHECK(esp_wifi_start());
    
    return ESP_OK;
}

esp_err_t wifi_module_disconnect(void)
{
    if (!wifi_state.initialized) {
        ESP_LOGE(TAG, "WiFi module not initialized");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Disconnecting WiFi");
    
    esp_err_t ret = esp_wifi_disconnect();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGE(TAG, "Failed to disconnect: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = esp_wifi_stop();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGE(TAG, "Failed to stop WiFi: %s", esp_err_to_name(ret));
        return ret;
    }
    
    wifi_state.connected = false;
    return ESP_OK;
}

bool wifi_module_is_connected(void)
{
    return wifi_state.connected;
}

esp_err_t wifi_module_get_credentials(wifi_credentials_t *credentials)
{
    if (!credentials) {
        return ESP_ERR_INVALID_ARG;
    }
    
    strlcpy(credentials->ssid, (char *)wifi_state.wifi_config.sta.ssid,
            sizeof(credentials->ssid));
    strlcpy(credentials->password, (char *)wifi_state.wifi_config.sta.password,
            sizeof(credentials->password));
    
    return ESP_OK;
}

esp_err_t wifi_module_save_credentials(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;
    
    ESP_LOGI(TAG, "Saving WiFi credentials to NVS");
    
    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Save SSID
    ret = nvs_set_str(nvs_handle, NVS_KEY_SSID, 
                     (char *)wifi_state.wifi_config.sta.ssid);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save SSID: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }
    
    // Save password
    ret = nvs_set_str(nvs_handle, NVS_KEY_PASS,
                     (char *)wifi_state.wifi_config.sta.password);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save password: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }
    
    // Commit changes
    ret = nvs_commit(nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(ret));
    }
    
    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "WiFi credentials saved");
    return ret;
}

esp_err_t wifi_module_load_credentials(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;
    size_t length;
    
    ESP_LOGI(TAG, "Loading WiFi credentials from NVS");
    
    ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No saved credentials found, using default configuration");
        
        // Use default credentials from Kconfig
        strcpy((char *)wifi_state.wifi_config.sta.ssid, CONFIG_AG_WIFI_SSID);
        strcpy((char *)wifi_state.wifi_config.sta.password, CONFIG_AG_WIFI_PASSWORD);
        
        ESP_LOGI(TAG, "Using default SSID: %s", CONFIG_AG_WIFI_SSID);
        return ESP_OK;
    }
    
    // Load SSID
    length = sizeof(wifi_state.wifi_config.sta.ssid);
    ret = nvs_get_str(nvs_handle, NVS_KEY_SSID,
                     (char *)wifi_state.wifi_config.sta.ssid, &length);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load SSID: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }
    
    // Load password
    length = sizeof(wifi_state.wifi_config.sta.password);
    ret = nvs_get_str(nvs_handle, NVS_KEY_PASS,
                     (char *)wifi_state.wifi_config.sta.password, &length);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load password: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }
    
    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "WiFi credentials loaded: SSID=%s", 
             wifi_state.wifi_config.sta.ssid);
    return ESP_OK;
}

esp_err_t wifi_module_clear_credentials(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;
    
    ESP_LOGI(TAG, "Clearing saved WiFi credentials");
    
    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }
    
    nvs_erase_key(nvs_handle, NVS_KEY_SSID);
    nvs_erase_key(nvs_handle, NVS_KEY_PASS);
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    ESP_LOGI(TAG, "WiFi credentials cleared");
    return ESP_OK;
}

esp_err_t wifi_module_get_mac(uint8_t mac[6])
{
    if (!wifi_state.initialized) {
        ESP_LOGE(TAG, "WiFi module not initialized");
        return ESP_FAIL;
    }
    
    return esp_wifi_get_mac(WIFI_IF_STA, mac);
}