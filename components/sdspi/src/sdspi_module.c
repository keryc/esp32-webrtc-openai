#include "sdspi_module.h"
#include <esp_log.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"
#include "esp_err.h"

static const char *TAG = "sdspi_module";

static struct {
    bool initialized;
    bool mounted;
    sdmmc_card_t *card;
    sdmmc_host_t host;
    sdspi_device_config_t slot_config;
    sdspi_config_t config;
    sdspi_event_callback_t event_callback;
} sdspi_state = {0};

static void notify_event(sdspi_event_t event, void *data)
{
    if (sdspi_state.event_callback) {
        sdspi_state.event_callback(event, data);
    }
}

esp_err_t sdspi_module_init(const sdspi_config_t *config, sdspi_event_callback_t callback)
{
    if (sdspi_state.initialized) {
        ESP_LOGW(TAG, "SDSPI module already initialized");
        return ESP_OK;
    }
    
    if (!config) {
        ESP_LOGE(TAG, "Invalid configuration");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Initializing SD card module");
    ESP_LOGI(TAG, "Using SPI peripheral");
    
    esp_err_t ret;
    
    memcpy(&sdspi_state.config, config, sizeof(sdspi_config_t));
    sdspi_state.event_callback = callback;
    
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = config->format_if_mount_failed,
        .max_files = SDSPI_MAX_FILES,
        .allocation_unit_size = SDSPI_ALLOCATION_UNIT_SIZE,
    };
    
    ESP_LOGI(TAG, "Initializing SPI bus");
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = config->mosi,
        .miso_io_num = config->miso,
        .sclk_io_num = config->clk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    
    sdspi_state.host = (sdmmc_host_t)SDSPI_HOST_DEFAULT();
    sdspi_state.host.max_freq_khz = config->max_freq_khz;
    
    ret = spi_bus_initialize(sdspi_state.host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        notify_event(SDSPI_EVENT_ERROR, (void*)&ret);
        return ret;
    }
    
    sdspi_state.slot_config = (sdspi_device_config_t)SDSPI_DEVICE_CONFIG_DEFAULT();
    sdspi_state.slot_config.gpio_cs = config->cs;
    sdspi_state.slot_config.host_id = sdspi_state.host.slot;
    
    ESP_LOGI(TAG, "Mounting filesystem");
    ret = esp_vfs_fat_sdspi_mount(SDSPI_MOUNT_POINT, &sdspi_state.host, 
                                  &sdspi_state.slot_config, &mount_config, 
                                  &sdspi_state.card);
    
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. "
                     "If you want the card to be formatted, set format_if_mount_failed = true.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                     "Make sure SD card lines have pull-up resistors in place.", 
                     esp_err_to_name(ret));
        }
        spi_bus_free(sdspi_state.host.slot);
        notify_event(SDSPI_EVENT_ERROR, (void*)&ret);
        return ret;
    }
    
    ESP_LOGI(TAG, "SD card mounted successfully");
    sdspi_state.mounted = true;
    sdspi_state.initialized = true;
    
    sdmmc_card_print_info(stdout, sdspi_state.card);
    
    notify_event(SDSPI_EVENT_MOUNTED, NULL);
    
    return ESP_OK;
}

esp_err_t sdspi_module_deinit(void)
{
    if (!sdspi_state.initialized) {
        ESP_LOGW(TAG, "SDSPI module not initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Deinitializing SD card module");
    
    if (sdspi_state.mounted) {
        esp_vfs_fat_sdcard_unmount(SDSPI_MOUNT_POINT, sdspi_state.card);
        sdspi_state.mounted = false;
    }
    
    spi_bus_free(sdspi_state.host.slot);
    
    memset(&sdspi_state, 0, sizeof(sdspi_state));
    
    notify_event(SDSPI_EVENT_UNMOUNTED, NULL);
    
    ESP_LOGI(TAG, "SD card module deinitialized");
    return ESP_OK;
}

esp_err_t sdspi_module_get_info(sdspi_info_t *info)
{
    if (!info) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!sdspi_state.initialized || !sdspi_state.mounted) {
        ESP_LOGE(TAG, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }
    
    FATFS *fs;
    DWORD free_clusters, total_sectors, free_sectors;
    
    if (f_getfree(SDSPI_MOUNT_POINT, &free_clusters, &fs) != FR_OK) {
        ESP_LOGE(TAG, "Failed to get free space");
        return ESP_FAIL;
    }
    
    total_sectors = (fs->n_fatent - 2) * fs->csize;
    free_sectors = free_clusters * fs->csize;
    uint32_t sector_size = fs->ssize;
    
    info->total_bytes = (uint64_t)total_sectors * sector_size;
    info->free_bytes = (uint64_t)free_sectors * sector_size;
    info->used_bytes = info->total_bytes - info->free_bytes;
    info->sector_size = sector_size;
    info->mounted = sdspi_state.mounted;
    
    if (sdspi_state.card) {
        if (sdspi_state.card->is_sdio) {
            info->type = "SDIO";
        } else if (sdspi_state.card->is_mmc) {
            info->type = "MMC";
        } else {
            // Check if it's high capacity card using the csd register
            // CSD version 2.0 indicates SDHC/SDXC
            if (sdspi_state.card->csd.csd_ver == 2) {
                info->type = "SDHC/SDXC";
            } else {
                info->type = "SDSC";
            }
        }
    } else {
        info->type = "Unknown";
    }
    
    return ESP_OK;
}

bool sdspi_module_is_mounted(void)
{
    return sdspi_state.mounted;
}

esp_err_t sdspi_module_format(void)
{
    if (!sdspi_state.initialized) {
        ESP_LOGE(TAG, "SDSPI module not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGW(TAG, "Formatting SD card - this will erase all data!");
    
    if (sdspi_state.mounted) {
        esp_vfs_fat_sdcard_unmount(SDSPI_MOUNT_POINT, sdspi_state.card);
        sdspi_state.mounted = false;
    }
    
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = SDSPI_MAX_FILES,
        .allocation_unit_size = SDSPI_ALLOCATION_UNIT_SIZE,
    };
    
    esp_err_t ret = esp_vfs_fat_sdspi_mount(SDSPI_MOUNT_POINT, &sdspi_state.host,
                                            &sdspi_state.slot_config, &mount_config,
                                            &sdspi_state.card);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to format and mount: %s", esp_err_to_name(ret));
        return ret;
    }
    
    sdspi_state.mounted = true;
    ESP_LOGI(TAG, "SD card formatted and mounted successfully");
    
    return ESP_OK;
}

esp_err_t sdspi_module_test(void)
{
    if (!sdspi_state.mounted) {
        ESP_LOGE(TAG, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Testing SD card write/read operations");
    
    const char *test_file = SDSPI_MOUNT_POINT "/test.txt";
    const char *test_data = "ESP32 SD Card Test - WebRTC Audio/Video Recorder\n";
    char read_buffer[128] = {0};
    
    FILE *f = fopen(test_file, "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return ESP_FAIL;
    }
    
    fprintf(f, "%s", test_data);
    fclose(f);
    ESP_LOGI(TAG, "File written: %s", test_file);
    
    f = fopen(test_file, "r");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return ESP_FAIL;
    }
    
    fgets(read_buffer, sizeof(read_buffer), f);
    fclose(f);
    
    if (strcmp(read_buffer, test_data) != 0) {
        ESP_LOGE(TAG, "Read data doesn't match written data");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Test successful - data verified");
    
    unlink(test_file);
    
    notify_event(SDSPI_EVENT_WRITE_COMPLETE, NULL);
    notify_event(SDSPI_EVENT_READ_COMPLETE, NULL);
    
    return ESP_OK;
}

const char* sdspi_module_get_mount_point(void)
{
    return SDSPI_MOUNT_POINT;
}

sdmmc_card_t* sdspi_module_get_card_handle(void)
{
    if (!sdspi_state.initialized) {
        return NULL;
    }
    return sdspi_state.card;
}