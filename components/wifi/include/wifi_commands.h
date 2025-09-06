#ifndef WIFI_COMMANDS_H
#define WIFI_COMMANDS_H

#include <esp_err.h>

/**
 * Register WiFi console commands
 * 
 * @return ESP_OK on success
 */
esp_err_t wifi_register_commands(void);

#endif // WIFI_COMMANDS_H