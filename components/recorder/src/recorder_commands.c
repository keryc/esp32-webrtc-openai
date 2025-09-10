#include "recorder_commands.h"
#include "recorder_module.h"
#include <esp_log.h>
#include <esp_console.h>
#include <argtable3/argtable3.h>
#include <string.h>

static const char *TAG = "recorder_cmd";


static struct {
    struct arg_str *action;
    struct arg_end *end;
} recorder_args;

static int recorder_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&recorder_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, recorder_args.end, argv[0]);
        return 1;
    }
    
    recorder_handle_t handle = (recorder_handle_t)recorder_get_handle();
    if (!handle) {
        ESP_LOGE(TAG, "Recorder not initialized. Is SD card mounted?");
        return 1;
    }
    
    const char *action = recorder_args.action->sval[0];
    
    if (strcmp(action, "start") == 0) {
        esp_err_t ret = recorder_start(handle);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Recording started successfully");
        } else {
            ESP_LOGE(TAG, "Failed to start recording: %s", esp_err_to_name(ret));
            return 1;
        }
    } else if (strcmp(action, "stop") == 0) {
        esp_err_t ret = recorder_stop(handle);
        if (ret == ESP_OK) {
            const char *filename = recorder_get_current_filename(handle);
            size_t bytes = recorder_get_bytes_written(handle);
            ESP_LOGI(TAG, "Recording stopped: %s (%.2f MB)", 
                     filename, bytes / (1024.0 * 1024.0));
        } else {
            ESP_LOGE(TAG, "Failed to stop recording: %s", esp_err_to_name(ret));
            return 1;
        }
    } else if (strcmp(action, "status") == 0) {
        recorder_state_t state = recorder_get_state(handle);
        const char *status = (state == RECORDER_STATE_RECORDING) ? "RECORDING" : "IDLE";
        ESP_LOGI(TAG, "Recording status: %s", status);
        if (state == RECORDER_STATE_RECORDING) {
            size_t bytes = recorder_get_bytes_written(handle);
            ESP_LOGI(TAG, "Current size: %.2f MB", bytes / (1024.0 * 1024.0));
        }
    } else {
        ESP_LOGE(TAG, "Unknown action: %s", action);
        return 1;
    }
    
    return 0;
}

esp_err_t recorder_commands_register(void)
{
    recorder_args.action = arg_str1(NULL, NULL, "<action>", "Action: start, stop, status");
    recorder_args.end = arg_end(2);
    
    const esp_console_cmd_t recorder_cmd_desc = {
        .command = "rec",
        .help = "Control audio recording (start/stop/status)",
        .hint = NULL,
        .func = &recorder_cmd,
        .argtable = &recorder_args
    };
    
    ESP_ERROR_CHECK(esp_console_cmd_register(&recorder_cmd_desc));
    ESP_LOGI(TAG, "Recorder commands registered");
    
    return ESP_OK;
}