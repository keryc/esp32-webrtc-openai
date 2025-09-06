#ifndef _SDSPI_MODULE_H_
#define _SDSPI_MODULE_H_

#include <esp_err.h>
#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "sdmmc_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDSPI_MOUNT_POINT "/sdcard"
#define SDSPI_MAX_FILES 10
#define SDSPI_ALLOCATION_UNIT_SIZE (16 * 1024)

typedef struct {
    gpio_num_t miso;
    gpio_num_t mosi;
    gpio_num_t clk;
    gpio_num_t cs;
    int max_freq_khz;
    bool format_if_mount_failed;
} sdspi_config_t;

typedef struct {
    uint64_t total_bytes;
    uint64_t free_bytes;
    uint64_t used_bytes;
    const char *type;
    uint32_t sector_size;
    bool mounted;
} sdspi_info_t;

typedef enum {
    SDSPI_EVENT_MOUNTED,
    SDSPI_EVENT_UNMOUNTED,
    SDSPI_EVENT_ERROR,
    SDSPI_EVENT_WRITE_COMPLETE,
    SDSPI_EVENT_READ_COMPLETE
} sdspi_event_t;

typedef void (*sdspi_event_callback_t)(sdspi_event_t event, void *data);

#define SDSPI_DEFAULT_CONFIG() { \
    .miso = GPIO_NUM_12, \
    .mosi = GPIO_NUM_3, \
    .clk = GPIO_NUM_11, \
    .cs = GPIO_NUM_2, \
    .max_freq_khz = 20000, \
    .format_if_mount_failed = false \
}

esp_err_t sdspi_module_init(const sdspi_config_t *config, sdspi_event_callback_t callback);

esp_err_t sdspi_module_deinit(void);

esp_err_t sdspi_module_get_info(sdspi_info_t *info);

bool sdspi_module_is_mounted(void);

esp_err_t sdspi_module_format(void);

esp_err_t sdspi_module_test(void);

const char* sdspi_module_get_mount_point(void);

sdmmc_card_t* sdspi_module_get_card_handle(void);

#ifdef __cplusplus
}
#endif

#endif