#ifndef WEBRTC_COMMANDS_H
#define WEBRTC_COMMANDS_H

#include <esp_err.h>

/**
 * Register WebRTC console commands
 * 
 * @return ESP_OK on success
 */
esp_err_t webrtc_register_commands(void);

#endif // WEBRTC_COMMANDS_H