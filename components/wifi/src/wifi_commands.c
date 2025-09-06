#include "wifi_commands.h"
#include "wifi_module.h"
#include <esp_console.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <argtable3/argtable3.h>
#include <stdio.h>
#include <stdlib.h>
#include "memory_manager.h"
static const char *TAG = "wifi_cmd";

// WiFi connect command arguments
static struct {
    struct arg_str *ssid;
    struct arg_str *password;
    struct arg_end *end;
} wifi_connect_args;

// WiFi connect command
static int cmd_wifi_connect(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&wifi_connect_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, wifi_connect_args.end, argv[0]);
        return 1;
    }
    
    const char *ssid = wifi_connect_args.ssid->sval[0];
    const char *password = wifi_connect_args.password->count > 0 ? 
                          wifi_connect_args.password->sval[0] : "";
    
    esp_err_t ret = wifi_module_connect(ssid, password);
    if (ret != ESP_OK) {
        printf("Failed to connect: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    printf("Connecting to '%s'...\n", ssid);
    return 0;
}

// WiFi disconnect command
static int cmd_wifi_disconnect(int argc, char **argv)
{
    esp_err_t ret = wifi_module_disconnect();
    if (ret != ESP_OK) {
        printf("Failed to disconnect: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    printf("WiFi disconnected\n");
    return 0;
}

// WiFi status command
static int cmd_wifi_status(int argc, char **argv)
{
    printf("WiFi Status:\n");
    printf("  Connected: %s\n", wifi_module_is_connected() ? "Yes" : "No");
    
    if (wifi_module_is_connected()) {
        wifi_credentials_t creds;
        if (wifi_module_get_credentials(&creds) == ESP_OK) {
            printf("  SSID: %s\n", creds.ssid);
        }
        
        uint8_t mac[6];
        if (wifi_module_get_mac(mac) == ESP_OK) {
            printf("  MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        }
    }
    
    return 0;
}

// WiFi scan command
static int cmd_wifi_scan(int argc, char **argv)
{
    printf("Scanning for WiFi networks...\n");
    
    uint16_t ap_count = 0;
    wifi_ap_record_t *ap_list = NULL;
    
    // Start scan
    esp_err_t ret = esp_wifi_scan_start(NULL, true);
    if (ret != ESP_OK) {
        printf("Failed to start scan: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    // Get scan results
    ret = esp_wifi_scan_get_ap_num(&ap_count);
    if (ret != ESP_OK) {
        printf("Failed to get AP count: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    if (ap_count == 0) {
        printf("No networks found\n");
        return 0;
    }
    
    ap_list = mem_alloc(ap_count * sizeof(wifi_ap_record_t), MEM_POLICY_PREFER_PSRAM, "wifi_ap_list");
    if (!ap_list) {
        printf("Memory allocation failed\n");
        return 1;
    }
    
    ret = esp_wifi_scan_get_ap_records(&ap_count, ap_list);
    if (ret != ESP_OK) {
        printf("Failed to get scan results: %s\n", esp_err_to_name(ret));
        mem_free(ap_list);
        return 1;
    }
    
    // Print results
    printf("\nFound %d networks:\n", ap_count);
    printf("%-32s | Channel | RSSI | Auth\n", "SSID");
    printf("---------------------------------+---------+------+------\n");
    
    for (int i = 0; i < ap_count; i++) {
        const char *auth_mode;
        switch (ap_list[i].authmode) {
            case WIFI_AUTH_OPEN:
                auth_mode = "Open";
                break;
            case WIFI_AUTH_WEP:
                auth_mode = "WEP";
                break;
            case WIFI_AUTH_WPA_PSK:
                auth_mode = "WPA";
                break;
            case WIFI_AUTH_WPA2_PSK:
                auth_mode = "WPA2";
                break;
            case WIFI_AUTH_WPA_WPA2_PSK:
                auth_mode = "WPA/2";
                break;
            case WIFI_AUTH_WPA3_PSK:
                auth_mode = "WPA3";
                break;
            default:
                auth_mode = "Other";
                break;
        }
        
        printf("%-32.32s | %7d | %4d | %s\n",
               ap_list[i].ssid,
               ap_list[i].primary,
               ap_list[i].rssi,
               auth_mode);
    }
    
    mem_free(ap_list);
    return 0;
}

// WiFi auto-connect command
static int cmd_wifi_auto(int argc, char **argv)
{
    esp_err_t ret = wifi_module_load_credentials();
    if (ret != ESP_OK) {
        printf("Failed to load credentials: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    wifi_credentials_t creds;
    wifi_module_get_credentials(&creds);
    
    printf("Auto-connecting to network: %s\n", creds.ssid);
    
    ret = wifi_module_connect(creds.ssid, creds.password);
    if (ret != ESP_OK) {
        printf("Failed to connect: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    return 0;
}

// WiFi clear credentials command
static int cmd_wifi_clear(int argc, char **argv)
{
    esp_err_t ret = wifi_module_clear_credentials();
    if (ret != ESP_OK) {
        printf("Failed to clear credentials: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    printf("WiFi credentials cleared\n");
    return 0;
}

esp_err_t wifi_register_commands(void)
{
    // WiFi connect command
    wifi_connect_args.ssid = arg_str1(NULL, NULL, "<ssid>", "WiFi network name");
    wifi_connect_args.password = arg_str0(NULL, NULL, "<password>", "WiFi password");
    wifi_connect_args.end = arg_end(2);
    
    const esp_console_cmd_t wifi_connect_cmd = {
        .command = "wifi",
        .help = "Connect to WiFi network",
        .hint = NULL,
        .func = &cmd_wifi_connect,
        .argtable = &wifi_connect_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&wifi_connect_cmd));
    
    // WiFi disconnect command
    const esp_console_cmd_t wifi_disconnect_cmd = {
        .command = "wifi_disconnect",
        .help = "Disconnect from WiFi",
        .hint = NULL,
        .func = &cmd_wifi_disconnect,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&wifi_disconnect_cmd));
    
    // WiFi status command
    const esp_console_cmd_t wifi_status_cmd = {
        .command = "wifi_status",
        .help = "Show WiFi connection status",
        .hint = NULL,
        .func = &cmd_wifi_status,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&wifi_status_cmd));
    
    // WiFi scan command
    const esp_console_cmd_t wifi_scan_cmd = {
        .command = "wifi_scan",
        .help = "Scan for available WiFi networks",
        .hint = NULL,
        .func = &cmd_wifi_scan,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&wifi_scan_cmd));
    
    // WiFi auto-connect command
    const esp_console_cmd_t wifi_auto_cmd = {
        .command = "wifi_auto",
        .help = "Connect using saved credentials",
        .hint = NULL,
        .func = &cmd_wifi_auto,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&wifi_auto_cmd));
    
    // WiFi clear credentials command
    const esp_console_cmd_t wifi_clear_cmd = {
        .command = "wifi_clear",
        .help = "Clear saved WiFi credentials",
        .hint = NULL,
        .func = &cmd_wifi_clear,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&wifi_clear_cmd));
    
    ESP_LOGI(TAG, "WiFi commands registered");
    return ESP_OK;
}