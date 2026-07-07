/**
 ******************************************************************************
 * @file        spi_sd.c
 * @version     V1.0
 * @brief       SD卡 驱动代码
 *****************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

 #include "spi_sd.h"

 sdmmc_card_t *card;                                                 /* SD / MMC卡结构 */
 const char mount_point[] = MOUNT_POINT;                             /* 挂载点/根目录 */
 esp_err_t ret = ESP_OK;
 esp_err_t mount_ret = ESP_FAIL;
 
 /**
  * @brief       SD卡初始化
  * @param       无
  * @retval      esp_err_t
  */
 esp_err_t sd_spi_init(void)
 {
     return sd_card_mount();
 }

 bool sd_card_is_mounted(void)
 {
     return (mount_ret == ESP_OK && card != NULL);
 }

 esp_err_t sd_card_mount(void)
 {
     ret = ESP_OK;

     if (sd_card_is_mounted())
     {
         return ESP_OK;
     }

     if (MY_SD_Handle == NULL)
     {
         ret = my_spi_init();

         if (ret != ESP_OK)
         {
             mount_ret = ret;
             card = NULL;
             return ret;
         }
     }

     esp_vfs_fat_sdmmc_mount_config_t mount_config = {
         .format_if_mount_failed = false,
         .max_files = 5,
         .allocation_unit_size = 4 * 1024 * sizeof(uint8_t)
     };

     sdmmc_host_t host = SDSPI_HOST_DEFAULT();

     sdspi_device_config_t slot_config = {0};
     slot_config.host_id   = host.slot;
     slot_config.gpio_cs   = GPIO_NUM_NC;
     slot_config.gpio_cd   = GPIO_NUM_NC;
     slot_config.gpio_wp   = GPIO_NUM_NC;
     slot_config.gpio_int  = GPIO_NUM_NC;

     sdmmc_card_t *new_card = NULL;

     SD_CS(0);
     mount_ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &new_card);
     SD_CS(1);

     if (mount_ret == ESP_OK)
     {
         card = new_card;
     }
     else
     {
         card = NULL;
     }

     vTaskDelay(pdMS_TO_TICKS(10));
     return mount_ret;
 }

 void sd_card_unmount(void)
 {
     if (mount_ret == ESP_OK && card != NULL)
     {
         esp_vfs_fat_sdcard_unmount(mount_point, card);
     }

     mount_ret = ESP_FAIL;
     card = NULL;
     SD_CS(1);
 }

 esp_err_t sd_card_check(void)
 {
     esp_err_t check_ret;

     if (!sd_card_is_mounted())
     {
         return ESP_ERR_INVALID_STATE;
     }

     SD_CS(0);
     check_ret = sdmmc_get_status(card);
     SD_CS(1);

     return check_ret;
 }
 
 /**
  * @brief       获取SD卡相关信息
  * @param       out_total_bytes：总大小
  * @param       out_free_bytes：剩余大小
  * @retval      无
  */
 void sd_get_fatfs_usage(size_t *out_total_bytes, size_t *out_free_bytes)
 {
     FATFS *fs;
     size_t free_clusters;
     int res = f_getfree("0:", (DWORD *)&free_clusters, &fs);
     assert(res == FR_OK);
     size_t total_sectors = (fs->n_fatent - 2) * fs->csize;
     size_t free_sectors = free_clusters * fs->csize;
 
     size_t sd_total = total_sectors / 1024;
     size_t sd_total_KB = sd_total * fs->ssize;
     size_t sd_free = free_sectors / 1024;
     size_t sd_free_KB = sd_free*fs->ssize;
 
     /* 假设总大小小于4GiB，对于SPI Flash应该为true */
     if (out_total_bytes != NULL)
     {
         *out_total_bytes = sd_total_KB;
     }
     
     if (out_free_bytes != NULL)
     {
         *out_free_bytes = sd_free_KB;
     }
 }