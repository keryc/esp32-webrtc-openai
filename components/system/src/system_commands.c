/*
 * System Commands Implementation
 * Console commands for monitoring and debugging
 */

#include "system_commands.h"
#include "memory_manager.h"
#include <esp_log.h>
#include <esp_console.h>
#include <esp_system.h>
#include <esp_chip_info.h>
#include <argtable3/argtable3.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sys_cmd";

// mem_status command
static int cmd_mem_status(int argc, char **argv)
{
    memory_manager_print_status();
    return 0;
}

// mem_tasks command
static int cmd_mem_tasks(int argc, char **argv)
{
    memory_manager_print_tasks();
    return 0;
}

// sys_info command
static int cmd_sys_info(int argc, char **argv)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    
    printf("========== System Information ==========\n");
    printf("Chip: ESP32-S3 (%d cores, %s)\n", 
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded flash" : "external flash");
    printf("Silicon revision: %d\n", chip_info.revision);
    printf("IDF Version: %s\n", esp_get_idf_version());
    
    // Get reset reason
    esp_reset_reason_t reset_reason = esp_reset_reason();
    const char* reset_str[] = {
        "UNKNOWN", "POWERON", "SW", "PANIC", "INT_WDT", "TASK_WDT",
        "WDT", "DEEPSLEEP", "BROWNOUT", "SDIO"
    };
    printf("Last reset: %s\n", reset_str[reset_reason]);
    
    // Uptime
    uint64_t uptime_us = esp_timer_get_time();
    uint32_t uptime_sec = uptime_us / 1000000;
    printf("Uptime: %lu seconds (%lu:%02lu:%02lu)\n", 
           uptime_sec, uptime_sec/3600, (uptime_sec/60)%60, uptime_sec%60);
    
    // Task info
    printf("Tasks running: %d\n", uxTaskGetNumberOfTasks());
    printf("========================================\n");
    
    return 0;
}

// stress_test command
static struct {
    struct arg_int *duration;
    struct arg_lit *memory;
    struct arg_lit *cpu;
    struct arg_end *end;
} stress_args;

static int cmd_stress_test(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &stress_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, stress_args.end, argv[0]);
        return 1;
    }
    
    int duration = stress_args.duration->ival[0];
    bool test_memory = stress_args.memory->count > 0;
    bool test_cpu = stress_args.cpu->count > 0;
    
    ESP_LOGI(TAG, "Starting stress test for %d seconds...", duration);
    
    if (test_memory) {
        ESP_LOGI(TAG, "Memory stress: allocating/freeing buffers");
        // Simple memory stress
        for (int i = 0; i < duration * 10; i++) {
            void* ptr = mem_alloc(1024 + (i % 10) * 1024, 
                                 MEM_POLICY_ADAPTIVE, "stress");
            if (ptr) {
                vTaskDelay(pdMS_TO_TICKS(50));
                mem_free(ptr);
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
    
    if (test_cpu) {
        ESP_LOGI(TAG, "CPU stress: computation loop");
        // Simple CPU stress
        volatile uint32_t counter = 0;
        uint64_t end_time = esp_timer_get_time() + (duration * 1000000);
        while (esp_timer_get_time() < end_time) {
            for (int i = 0; i < 10000; i++) {
                counter++;
            }
            if (counter % 1000000 == 0) {
                vTaskDelay(1); // Yield occasionally
            }
        }
    }
    
    ESP_LOGI(TAG, "Stress test completed");
    memory_manager_print_status();
    
    return 0;
}

// restart command
static int cmd_restart(int argc, char **argv)
{
    ESP_LOGI(TAG, "Restarting in 3 seconds...");
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
    return 0;
}

esp_err_t system_commands_register(void)
{
    ESP_LOGI(TAG, "Registering system commands");
    
    // mem_status command
    const esp_console_cmd_t mem_status_cmd = {
        .command = "mem_status",
        .help = "Show current memory status",
        .hint = NULL,
        .func = &cmd_mem_status,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&mem_status_cmd));
    
    // mem_tasks command
    const esp_console_cmd_t mem_tasks_cmd = {
        .command = "mem_tasks",
        .help = "Show task stack usage",
        .hint = NULL,
        .func = &cmd_mem_tasks,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&mem_tasks_cmd));
    
    // sys_info command
    const esp_console_cmd_t sys_info_cmd = {
        .command = "sys_info",
        .help = "Show system information",
        .hint = NULL,
        .func = &cmd_sys_info,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&sys_info_cmd));
    
    // stress_test command
    stress_args.duration = arg_int1(NULL, NULL, "<seconds>", "Test duration");
    stress_args.memory = arg_lit0("m", "memory", "Test memory allocation");
    stress_args.cpu = arg_lit0("c", "cpu", "Test CPU load");
    stress_args.end = arg_end(2);
    
    const esp_console_cmd_t stress_cmd = {
        .command = "stress_test",
        .help = "Run stress test",
        .hint = "<seconds> [-m] [-c]",
        .func = &cmd_stress_test,
        .argtable = &stress_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&stress_cmd));
    
    // restart command
    const esp_console_cmd_t restart_cmd = {
        .command = "restart",
        .help = "Restart the system",
        .hint = NULL,
        .func = &cmd_restart,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&restart_cmd));
    
    ESP_LOGI(TAG, "System commands registered successfully");
    return ESP_OK;
}