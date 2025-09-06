#include "audio_commands.h"
#include "audio_module.h"
#include <esp_console.h>
#include <esp_log.h>
#include <argtable3/argtable3.h>
#include <stdio.h>
#include <stdlib.h>
static const char *TAG = "audio_cmd";

// Audio start command
static int cmd_audio_start(int argc, char **argv)
{
    printf("Starting audio system...\n");
    
    esp_err_t ret = audio_module_start();
    if (ret != ESP_OK) {
        printf("Failed to start audio system: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    printf("Audio system started\n");
    return 0;
}

// Audio stop command
static int cmd_audio_stop(int argc, char **argv)
{
    printf("Stopping audio system...\n");
    
    esp_err_t ret = audio_module_stop();
    if (ret != ESP_OK) {
        printf("Failed to stop audio system: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    printf("Audio system stopped\n");
    return 0;
}

// Audio status command
static int cmd_audio_status(int argc, char **argv)
{
    printf("Audio System Status:\n");
    printf("  Ready: %s\n", audio_module_is_ready() ? "Yes" : "No");
    printf("  Volume: %d%%\n", audio_module_get_volume());
    
    return 0;
}

// Audio volume command arguments
static struct {
    struct arg_int *level;
    struct arg_end *end;
} audio_volume_args;

// Audio volume command
static int cmd_audio_volume(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&audio_volume_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, audio_volume_args.end, argv[0]);
        return 1;
    }
    
    int volume = audio_volume_args.level->ival[0];
    
    esp_err_t ret = audio_module_set_volume(volume);
    if (ret != ESP_OK) {
        printf("Failed to set volume: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    printf("Volume set to %d%%\n", volume);
    return 0;
}

// Audio test command
static int cmd_audio_test(int argc, char **argv)
{
    printf("Starting audio loopback test...\n");
    printf("You should hear your microphone input through speakers\n");
    printf("Press any key to stop test\n");
    
    esp_err_t ret = audio_module_test_loopback();
    if (ret != ESP_OK) {
        printf("Audio test failed: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    printf("Audio test completed\n");
    return 0;
}

esp_err_t audio_register_commands(void)
{
    // Audio start command
    const esp_console_cmd_t audio_start_cmd = {
        .command = "audio_start",
        .help = "Start audio system",
        .hint = NULL,
        .func = &cmd_audio_start,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&audio_start_cmd));
    
    // Audio stop command
    const esp_console_cmd_t audio_stop_cmd = {
        .command = "audio_stop",
        .help = "Stop audio system",
        .hint = NULL,
        .func = &cmd_audio_stop,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&audio_stop_cmd));
    
    // Audio status command
    const esp_console_cmd_t audio_status_cmd = {
        .command = "audio_status",
        .help = "Show audio system status",
        .hint = NULL,
        .func = &cmd_audio_status,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&audio_status_cmd));
    
    // Audio volume command
    audio_volume_args.level = arg_int1(NULL, NULL, "<level>", "Volume level (0-100)");
    audio_volume_args.end = arg_end(2);
    
    const esp_console_cmd_t audio_volume_cmd = {
        .command = "audio_volume",
        .help = "Set audio volume",
        .hint = NULL,
        .func = &cmd_audio_volume,
        .argtable = &audio_volume_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&audio_volume_cmd));
    
    // Audio test command
    const esp_console_cmd_t audio_test_cmd = {
        .command = "audio_test",
        .help = "Test audio capture and playback",
        .hint = NULL,
        .func = &cmd_audio_test,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&audio_test_cmd));
    
    ESP_LOGI(TAG, "Audio commands registered");
    return ESP_OK;
}