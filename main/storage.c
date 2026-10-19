#include "storage.h"
#include "config.h"

#include <stdbool.h>
#include <stdio.h>

#include "esp_err.h"
#include "esp_log.h"

#include "driver/spi_master.h"

#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"


static const char *TAG = "STORAGE";

static sdmmc_card_t *card = NULL;

// -------------------------------------------------------

esp_err_t storage_mount(void)
{
    ESP_LOGI(TAG, "Inicializando SD...");

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();

    // Arrancamos conservadores por Dupont / prototipo.
    host.max_freq_khz = 1000;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_MOSI,
        .miso_io_num = SD_MISO,
        .sclk_io_num = SD_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096
    };

    esp_err_t ret =
        spi_bus_initialize(
            host.slot,
            &bus_cfg,
            SDSPI_DEFAULT_DMA
        );

    if (ret != ESP_OK) {
        ESP_LOGE(
            TAG,
            "spi_bus_initialize fallo: %s",
            esp_err_to_name(ret)
        );

        return ret;
    }

    sdspi_device_config_t slot_config =
        SDSPI_DEVICE_CONFIG_DEFAULT();

    slot_config.gpio_cs = SD_CS;
    slot_config.host_id = host.slot;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    ret = esp_vfs_fat_sdspi_mount(
        SD_MOUNT_POINT,
        &host,
        &slot_config,
        &mount_config,
        &card
    );

    if (ret != ESP_OK) {
        ESP_LOGE(
            TAG,
            "No pude montar SD: %s",
            esp_err_to_name(ret)
        );

        spi_bus_free(host.slot);
        return ret;
    }

    ESP_LOGI(TAG, "SD OK");

    sdmmc_card_print_info(stdout, card);

    return ESP_OK;
}
void storage_unmount(void)
{
    if (card == NULL)
    {
        return;
    }

    esp_vfs_fat_sdcard_unmount(
        SD_MOUNT_POINT,
        card
    );

    card = NULL;

    ESP_LOGI(
        TAG,
        "SD desmontada"
    );
}