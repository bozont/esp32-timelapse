/* ESP32 timelapse camera
*/
#include <stdio.h>
#include <esp_log.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"

#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"

#include "esp_camera.h"
static const char *TAG = "tcam";
#define BOARD_ESP32CAM_AITHINKER
//#define SD_MODE_SDMMC


#define PIN_NUM_SD_MISO 2
#define PIN_NUM_SD_MOSI 15
#define PIN_NUM_SD_CLK  14
#define PIN_NUM_SD_CS   13


#ifdef BOARD_ESP32CAM_AITHINKER

#define CAM_PIN_PWDN 32
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK 0
#define CAM_PIN_SIOD 26
#define CAM_PIN_SIOC 27

#define CAM_PIN_D7 35
#define CAM_PIN_D6 34
#define CAM_PIN_D5 39
#define CAM_PIN_D4 36
#define CAM_PIN_D3 21
#define CAM_PIN_D2 19
#define CAM_PIN_D1 18
#define CAM_PIN_D0 5
#define CAM_PIN_VSYNC 25
#define CAM_PIN_HREF 23
#define CAM_PIN_PCLK 22

#endif


void camera_task() {
    camera_config_t camera_config = {
        .pin_pwdn = CAM_PIN_PWDN,
        .pin_reset = CAM_PIN_RESET,
        .pin_xclk = CAM_PIN_XCLK,
        .pin_sscb_sda = CAM_PIN_SIOD,
        .pin_sscb_scl = CAM_PIN_SIOC,

        .pin_d7 = CAM_PIN_D7,
        .pin_d6 = CAM_PIN_D6,
        .pin_d5 = CAM_PIN_D5,
        .pin_d4 = CAM_PIN_D4,
        .pin_d3 = CAM_PIN_D3,
        .pin_d2 = CAM_PIN_D2,
        .pin_d1 = CAM_PIN_D1,
        .pin_d0 = CAM_PIN_D0,
        .pin_vsync = CAM_PIN_VSYNC,
        .pin_href = CAM_PIN_HREF,
        .pin_pclk = CAM_PIN_PCLK,

        //XCLK 20MHz or 10MHz for OV2640 double FPS (Experimental)
        .xclk_freq_hz = 20000000,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,

        .pixel_format = PIXFORMAT_JPEG, //YUV422,GRAYSCALE,RGB565,JPEG
        .frame_size = FRAMESIZE_UXGA,   /* 1600x1200 */

        .jpeg_quality = 12, //0-63 lower number means higher quality
        .fb_count = 1       //if more than one, i2s runs in continuous mode. Use only with JPEG
    };

    esp_err_t err = esp_camera_init(&camera_config);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Camera Init Failed");
        return;
    }

    esp_err_t ret;
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_card_t* card;
#ifdef SD_MODE_SDMMC
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_1;
    host.max_freq_khz = 40000;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;

    ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);
#else
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = 20000;
    host.slot = HSPI_HOST;
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_SD_MOSI,
        .miso_io_num = PIN_NUM_SD_MISO,
        .sclk_io_num = PIN_NUM_SD_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    ret = spi_bus_initialize(host.slot, &bus_cfg, 2);
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "Failed to initialize SPI bus");
        return;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_SD_CS;
    slot_config.host_id = host.slot;

    ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config, &card);
#endif

    if(ret != ESP_OK) {
        if(ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Init_SD: Failed to mount filesystem.");
        } else {
           ESP_LOGE(TAG, "Init_SD: Failed to initialize the card. %d", ret);
        }
        return;
    }
    ESP_LOGI(TAG, "Init_SD: init successful. Card info:\n");
    sdmmc_card_print_info(stdout, card);

    mkdir("/sdcard/timelapse", 0775);
    uint32_t img_cnt = 0u;

    while(1) {
        ESP_LOGI(TAG, "Capturing image...");
        camera_fb_t *pic = esp_camera_fb_get();
        ESP_LOGI(TAG, "Image captured. Size: %zu bytes", pic->len);

        char filename[50];
        sprintf(filename, "/sdcard/timelapse/img_%08d.jpg", img_cnt);
        FILE* file = fopen(filename, "wb");
        if(!file) {
           ESP_LOGE(TAG, "SD: Failed to open new file (%s)", filename);
        } else {
            img_cnt++;
            fwrite(pic->buf, sizeof(uint8_t), pic->len, file);
            fclose(file);
            ESP_LOGI(TAG, "SD: File written (%s)", filename);
        }

        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void app_main(void) {
    printf("ESP32 timelapse camera\n");
    xTaskCreatePinnedToCore(camera_task, "camera task", configMINIMAL_STACK_SIZE * 15, NULL, 5, NULL, PRO_CPU_NUM);
}
