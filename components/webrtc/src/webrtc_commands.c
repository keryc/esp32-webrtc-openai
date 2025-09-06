#include "webrtc_commands.h"
#include "webrtc_module.h"
#include <esp_console.h>
#include <esp_log.h>
#include <argtable3/argtable3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char *TAG = "webrtc_cmd";

// WebRTC start command
static int cmd_webrtc_start(int argc, char **argv)
{
    printf("Starting WebRTC session...\n");
    
    esp_err_t ret = webrtc_module_start();
    if (ret != ESP_OK) {
        printf("Failed to start WebRTC: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    printf("WebRTC session started\n");
    return 0;
}

// WebRTC stop command
static int cmd_webrtc_stop(int argc, char **argv)
{
    printf("Stopping WebRTC session...\n");
    
    esp_err_t ret = webrtc_module_stop();
    if (ret != ESP_OK) {
        printf("Failed to stop WebRTC: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    printf("WebRTC session stopped\n");
    return 0;
}

// WebRTC status command
static int cmd_webrtc_status(int argc, char **argv)
{
    webrtc_state_t state = webrtc_module_get_state();
    const char *state_str[] = {"DISCONNECTED", "CONNECTING", "CONNECTED", "FAILED"};
    
    printf("WebRTC Status:\n");
    printf("  State: %s\n", state_str[state]);
    printf("  Connected: %s\n", webrtc_module_is_connected() ? "Yes" : "No");
    
    // Query detailed status
    webrtc_module_query_status();
    
    return 0;
}

// WebRTC send text command arguments
static struct {
    struct arg_str *message;
    struct arg_end *end;
} webrtc_send_args;

// WebRTC send text command
static int cmd_webrtc_send(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&webrtc_send_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, webrtc_send_args.end, argv[0]);
        return 1;
    }
    
    const char *message = webrtc_send_args.message->sval[0];
    
    esp_err_t ret = webrtc_module_send_text(message);
    if (ret != ESP_OK) {
        printf("Failed to send message: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    printf("Message sent: \"%s\"\n", message);
    return 0;
}

// WebRTC query command
static int cmd_webrtc_query(int argc, char **argv)
{
    printf("Querying WebRTC status...\n");
    
    esp_err_t ret = webrtc_module_query_status();
    if (ret != ESP_OK) {
        printf("Failed to query status: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    return 0;
}

esp_err_t webrtc_register_commands(void)
{
    // WebRTC start command
    const esp_console_cmd_t webrtc_start_cmd = {
        .command = "webrtc_start",
        .help = "Start WebRTC session",
        .hint = NULL,
        .func = &cmd_webrtc_start,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&webrtc_start_cmd));
    
    // WebRTC stop command
    const esp_console_cmd_t webrtc_stop_cmd = {
        .command = "webrtc_stop",
        .help = "Stop WebRTC session",
        .hint = NULL,
        .func = &cmd_webrtc_stop,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&webrtc_stop_cmd));
    
    // WebRTC status command
    const esp_console_cmd_t webrtc_status_cmd = {
        .command = "webrtc_status",
        .help = "Show WebRTC connection status",
        .hint = NULL,
        .func = &cmd_webrtc_status,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&webrtc_status_cmd));
    
    // WebRTC send text command
    webrtc_send_args.message = arg_str1(NULL, NULL, "<message>", "Text message to send to OpenAI");
    webrtc_send_args.end = arg_end(2);
    
    const esp_console_cmd_t webrtc_send_cmd = {
        .command = "webrtc_send",
        .help = "Send text message to OpenAI",
        .hint = NULL,
        .func = &cmd_webrtc_send,
        .argtable = &webrtc_send_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&webrtc_send_cmd));
    
    // WebRTC query command
    const esp_console_cmd_t webrtc_query_cmd = {
        .command = "webrtc_query",
        .help = "Query WebRTC connection status",
        .hint = NULL,
        .func = &cmd_webrtc_query,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&webrtc_query_cmd));
    
    ESP_LOGI(TAG, "WebRTC commands registered");
    return ESP_OK;
}