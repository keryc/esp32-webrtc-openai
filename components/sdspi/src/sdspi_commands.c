#include "sdspi_commands.h"
#include "sdspi_module.h"
#include <esp_log.h>
#include <esp_console.h>
#include <argtable3/argtable3.h>
#include <string.h>

static const char *TAG = "sdspi_cmd";

static int cmd_sd_info(int argc, char **argv)
{
    if (!sdspi_module_is_mounted()) {
        printf("SD card not mounted. Use 'sd mount' first.\n");
        return 1;
    }
    
    sdspi_info_t info;
    esp_err_t ret = sdspi_module_get_info(&info);
    if (ret != ESP_OK) {
        printf("Failed to get SD card info: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    printf("\n📁 SD Card Information:\n");
    printf("========================\n");
    printf("Type:        %s\n", info.type);
    printf("Status:      %s\n", info.mounted ? "Mounted" : "Not mounted");
    printf("Mount point: %s\n", sdspi_module_get_mount_point());
    printf("\n");
    printf("Storage:\n");
    printf("  Total:     %.2f GB\n", info.total_bytes / (1024.0 * 1024.0 * 1024.0));
    printf("  Used:      %.2f GB (%.1f%%)\n", 
           info.used_bytes / (1024.0 * 1024.0 * 1024.0),
           (info.used_bytes * 100.0) / info.total_bytes);
    printf("  Free:      %.2f GB\n", info.free_bytes / (1024.0 * 1024.0 * 1024.0));
    printf("  Sector:    %lu bytes\n", info.sector_size);
    printf("\n");
    
    return 0;
}

static int cmd_sd_mount(int argc, char **argv)
{
    if (sdspi_module_is_mounted()) {
        printf("SD card already mounted\n");
        return 0;
    }
    
    printf("Mounting SD card...\n");
    
    sdspi_config_t config = SDSPI_DEFAULT_CONFIG();
    esp_err_t ret = sdspi_module_init(&config, NULL);
    
    if (ret != ESP_OK) {
        printf("Failed to mount SD card: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    printf("✅ SD card mounted successfully\n");
    return cmd_sd_info(0, NULL);
}

static int cmd_sd_unmount(int argc, char **argv)
{
    if (!sdspi_module_is_mounted()) {
        printf("SD card not mounted\n");
        return 0;
    }
    
    printf("Unmounting SD card...\n");
    
    esp_err_t ret = sdspi_module_deinit();
    if (ret != ESP_OK) {
        printf("Failed to unmount SD card: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    printf("SD card unmounted\n");
    return 0;
}

static int cmd_sd_test(int argc, char **argv)
{
    if (!sdspi_module_is_mounted()) {
        printf("SD card not mounted. Use 'sd mount' first.\n");
        return 1;
    }
    
    printf("Testing SD card read/write...\n");
    
    esp_err_t ret = sdspi_module_test();
    if (ret != ESP_OK) {
        printf("❌ SD card test failed: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    printf("✅ SD card test passed\n");
    return 0;
}

static int cmd_sd_format(int argc, char **argv)
{
    if (!sdspi_module_is_mounted()) {
        printf("SD card not mounted. Use 'sd mount' first.\n");
        return 1;
    }
    
    printf("⚠️  WARNING: This will erase all data on the SD card!\n");
    printf("Type 'YES' to confirm: ");
    
    char confirm[10];
    if (fgets(confirm, sizeof(confirm), stdin) == NULL) {
        printf("Format cancelled\n");
        return 1;
    }
    
    if (strncmp(confirm, "YES", 3) != 0) {
        printf("Format cancelled\n");
        return 1;
    }
    
    printf("Formatting SD card...\n");
    
    esp_err_t ret = sdspi_module_format();
    if (ret != ESP_OK) {
        printf("Failed to format SD card: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    printf("✅ SD card formatted successfully\n");
    return 0;
}

static int cmd_sd(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: sd <mount|unmount|info|test|format>\n");
        return 1;
    }
    
    if (strcmp(argv[1], "mount") == 0) {
        return cmd_sd_mount(argc - 1, &argv[1]);
    } else if (strcmp(argv[1], "unmount") == 0) {
        return cmd_sd_unmount(argc - 1, &argv[1]);
    } else if (strcmp(argv[1], "info") == 0) {
        return cmd_sd_info(argc - 1, &argv[1]);
    } else if (strcmp(argv[1], "test") == 0) {
        return cmd_sd_test(argc - 1, &argv[1]);
    } else if (strcmp(argv[1], "format") == 0) {
        return cmd_sd_format(argc - 1, &argv[1]);
    } else {
        printf("Unknown subcommand: %s\n", argv[1]);
        printf("Usage: sd <mount|unmount|info|test|format>\n");
        return 1;
    }
}

esp_err_t sdspi_commands_register(void)
{
    const esp_console_cmd_t sd_cmd = {
        .command = "sd",
        .help = "SD card operations (mount/unmount/info/test/format)",
        .hint = NULL,
        .func = &cmd_sd,
    };
    
    ESP_LOGI(TAG, "Registering SD card commands");
    return esp_console_cmd_register(&sd_cmd);
}