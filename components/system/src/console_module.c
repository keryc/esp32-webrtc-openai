#include "console_module.h"
#include <stdio.h>
#include <string.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_console.h>
#include <esp_vfs_dev.h>
#include <driver/uart.h>
#include <driver/uart_vfs.h>
#include <linenoise/linenoise.h>
#include <argtable3/argtable3.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
static const char *TAG = "console";

#define PROMPT_STR "board> "
#define MAX_CMDLINE_ARGS 16
#define MAX_CMDLINE_LENGTH 256

// Console task handle
static TaskHandle_t console_task_handle = NULL;

// Simple test command for initial testing
static int cmd_hello(int argc, char **argv)
{
    printf("Hello from ESP32 Console!\n");
    return 0;
}

esp_err_t console_module_init(void)
{
    ESP_LOGI(TAG, "Initializing console module");
    
    // Disable buffering on stdin
    setvbuf(stdin, NULL, _IONBF, 0);
    
    // Initialize VFS & UART (using new API)
    uart_vfs_dev_port_set_rx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CR);
    uart_vfs_dev_port_set_tx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CRLF);
    
    // Configure UART
    const uart_config_t uart_config = {
        .baud_rate = CONFIG_ESP_CONSOLE_UART_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    ESP_ERROR_CHECK(uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, 256, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(CONFIG_ESP_CONSOLE_UART_NUM, &uart_config));
    
    // Tell VFS to use UART driver (using new API)
    uart_vfs_dev_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);
    
    // Initialize esp_console
    esp_console_config_t console_config = {
        .max_cmdline_args = MAX_CMDLINE_ARGS,
        .max_cmdline_length = MAX_CMDLINE_LENGTH,
        .hint_color = 36,  // Cyan color code
        .hint_bold = 1
    };
    
    esp_err_t ret = esp_console_init(&console_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize console: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Configure linenoise
    linenoiseSetMultiLine(1);
    linenoiseSetCompletionCallback(&esp_console_get_completion);
    linenoiseSetHintsCallback((linenoiseHintsCallback *)&esp_console_get_hint);
    linenoiseHistorySetMaxLen(100);
    linenoiseAllowEmpty(false);
    
    ESP_LOGI(TAG, "Console module initialized");
    return ESP_OK;
}

esp_err_t console_register_commands(void)
{
    ESP_LOGI(TAG, "Registering console commands");
    
    // Register help command
    esp_console_register_help_command();
    
    // Register hello test command
    const esp_console_cmd_t hello_cmd = {
        .command = "hello",
        .help = "Print hello message",
        .hint = NULL,
        .func = &cmd_hello,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&hello_cmd));
    
    ESP_LOGI(TAG, "Commands registered");
    return ESP_OK;
}

static void console_task(void *pvParameters)
{
    const char *prompt = PROMPT_STR;
    
    printf("\n");
    printf("=====================================\n");
    printf("   ESP32 Console - Refactored\n");
    printf("   Type 'help' for commands\n");
    printf("=====================================\n");
    printf("\n");
    
    while (1) {
        char *line = linenoise(prompt);
        if (line == NULL) {
            continue;
        }
        
        if (strlen(line) > 0) {
            linenoiseHistoryAdd(line);
            
            int ret;
            esp_err_t err = esp_console_run(line, &ret);
            
            if (err == ESP_ERR_NOT_FOUND) {
                printf("Unknown command: %s\n", line);
            } else if (err == ESP_ERR_INVALID_ARG) {
                // Command was empty
            } else if (err == ESP_OK && ret != ESP_OK) {
                printf("Command error: 0x%x (%s)\n", ret, esp_err_to_name(ret));
            } else if (err != ESP_OK) {
                printf("Internal error: %s\n", esp_err_to_name(err));
            }
        }
        
        linenoiseFree(line);
    }
    
    vTaskDelete(NULL);
}

esp_err_t console_module_start(void)
{
    ESP_LOGI(TAG, "Starting console task");
    
    BaseType_t ret = xTaskCreate(
        console_task,
        "console",
        8192,  // Increased from 4096 to 8KB for WebRTC compatibility
        NULL,
        5,
        &console_task_handle
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create console task");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Console task started");
    return ESP_OK;
}