#ifndef WIFI_MODULE_H
#define WIFI_MODULE_H

#include <esp_err.h>
#include <stdbool.h>

// WiFi event callback type
typedef void (*wifi_event_callback_t)(bool connected);

// WiFi credentials structure
typedef struct {
    char ssid[32];
    char password[64];
} wifi_credentials_t;

/**
 * Initialize WiFi module
 * Sets up WiFi station mode and event handlers
 * 
 * @param callback Function to call when WiFi connects/disconnects (can be NULL)
 * @return ESP_OK on success
 */
esp_err_t wifi_module_init(wifi_event_callback_t callback);

/**
 * Connect to WiFi network
 * 
 * @param ssid Network SSID
 * @param password Network password
 * @return ESP_OK on success
 */
esp_err_t wifi_module_connect(const char *ssid, const char *password);

/**
 * Disconnect from WiFi network
 * 
 * @return ESP_OK on success
 */
esp_err_t wifi_module_disconnect(void);

/**
 * Get current connection status
 * 
 * @return true if connected, false otherwise
 */
bool wifi_module_is_connected(void);

/**
 * Get current WiFi credentials
 * 
 * @param credentials Pointer to store credentials
 * @return ESP_OK on success
 */
esp_err_t wifi_module_get_credentials(wifi_credentials_t *credentials);

/**
 * Save WiFi credentials to NVS
 * 
 * @return ESP_OK on success
 */
esp_err_t wifi_module_save_credentials(void);

/**
 * Load WiFi credentials from NVS
 * 
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if no saved credentials
 */
esp_err_t wifi_module_load_credentials(void);

/**
 * Clear saved WiFi credentials from NVS
 * 
 * @return ESP_OK on success
 */
esp_err_t wifi_module_clear_credentials(void);

/**
 * Get WiFi MAC address
 * 
 * @param mac Buffer to store MAC address (6 bytes)
 * @return ESP_OK on success
 */
esp_err_t wifi_module_get_mac(uint8_t mac[6]);

#endif // WIFI_MODULE_H