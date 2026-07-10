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
 typedef enum
 {
     SD_OWNER_NONE,
     SD_OWNER_LOCAL,
     SD_OWNER_USB
 } sd_owner_t;

 static esp_err_t mount_ret = ESP_FAIL;
 static SemaphoreHandle_t sd_mutex = NULL;
 static sd_owner_t sd_owner = SD_OWNER_NONE;
 static FATFS *sd_saved_fs = NULL;
 static uint32_t sd_local_sessions = 0;
 static bool sd_local_mounted = false;
 static bool sd_recovery_required = false;

 static esp_err_t sd_mutex_init(void)
 {
     if (sd_mutex == NULL)
     {
         sd_mutex = xSemaphoreCreateRecursiveMutex();
         if (sd_mutex == NULL)
         {
             return ESP_ERR_NO_MEM;
         }
     }

     return ESP_OK;
 }

 static bool sd_lock(TickType_t timeout)
 {
     return sd_mutex != NULL && xSemaphoreTakeRecursive(sd_mutex, timeout) == pdTRUE;
 }

 static void sd_unlock(void)
 {
     xSemaphoreGiveRecursive(sd_mutex);
 }

 static void sd_local_session_finish(bool recovery_required)
 {
     if (!sd_lock(portMAX_DELAY))
     {
         return;
     }

     if (recovery_required)
     {
         sd_recovery_required = true;
     }

     if (sd_local_sessions > 0)
     {
         sd_local_sessions--;
     }

     sd_unlock();
 }
 
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
     if (!sd_lock(portMAX_DELAY))
     {
         return false;
     }

     bool mounted = mount_ret == ESP_OK && card != NULL && sd_local_mounted &&
                    sd_owner == SD_OWNER_LOCAL && !sd_recovery_required;
     sd_unlock();
     return mounted;
 }

 bool sd_card_is_available(void)
 {
     if (!sd_lock(portMAX_DELAY))
     {
         return false;
     }

     bool available = mount_ret == ESP_OK && card != NULL;
     sd_unlock();
     return available;
 }

 esp_err_t sd_card_mount(void)
 {
     esp_err_t ret = sd_mutex_init();
     if (ret != ESP_OK)
     {
         return ret;
     }

     if (!sd_lock(portMAX_DELAY))
     {
         return ESP_ERR_TIMEOUT;
     }

     if (sd_recovery_required)
     {
         if (sd_owner == SD_OWNER_USB || sd_local_sessions != 0)
         {
             sd_unlock();
             return ESP_ERR_INVALID_STATE;
         }

         if (mount_ret == ESP_OK && card != NULL)
         {
             SD_CS(0);
             esp_vfs_fat_sdcard_unmount(mount_point, card);
             SD_CS(1);
         }

         mount_ret = ESP_FAIL;
         card = NULL;
         sd_saved_fs = NULL;
         sd_owner = SD_OWNER_NONE;
         sd_local_mounted = false;
         sd_recovery_required = false;
     }

     if (mount_ret == ESP_OK && card != NULL && sd_local_mounted && sd_owner == SD_OWNER_LOCAL)
     {
         sd_unlock();
         return ESP_OK;
     }

     if (sd_owner == SD_OWNER_USB)
     {
         sd_unlock();
         return ESP_ERR_INVALID_STATE;
     }

     if (mount_ret == ESP_OK && card != NULL && sd_saved_fs != NULL)
     {
         SD_CS(0);
         FRESULT fs_result = f_mount(sd_saved_fs, "0:", 1);
         SD_CS(1);
         if (fs_result == FR_OK)
         {
             sd_local_mounted = true;
             sd_owner = SD_OWNER_LOCAL;
             sd_recovery_required = false;
             sd_unlock();
             return ESP_OK;
         }

         SD_CS(0);
         esp_vfs_fat_sdcard_unmount(mount_point, card);
         SD_CS(1);
         mount_ret = ESP_FAIL;
         card = NULL;
         sd_saved_fs = NULL;
         sd_owner = SD_OWNER_NONE;
         sd_local_mounted = false;
         sd_recovery_required = false;
         sd_unlock();
         return ESP_FAIL;
     }

     if (MY_SD_Handle == NULL)
     {
         ret = my_spi_init();

         if (ret != ESP_OK)
         {
             mount_ret = ret;
             card = NULL;
             sd_owner = SD_OWNER_NONE;
             sd_local_mounted = false;
             sd_recovery_required = false;
             sd_unlock();
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
         sd_owner = SD_OWNER_LOCAL;
         sd_local_mounted = true;
         sd_saved_fs = NULL;
         sd_recovery_required = false;
     }
     else
     {
         card = NULL;
         sd_owner = SD_OWNER_NONE;
         sd_local_mounted = false;
         sd_recovery_required = false;
     }

     sd_unlock();
     vTaskDelay(pdMS_TO_TICKS(10));
     return mount_ret;
 }

 void sd_card_unmount(void)
 {
     if (!sd_lock(portMAX_DELAY))
     {
         return;
     }

     if (sd_owner == SD_OWNER_USB || sd_local_sessions != 0)
     {
         sd_unlock();
         return;
     }

     if (mount_ret == ESP_OK && card != NULL)
     {
         SD_CS(0);
         esp_vfs_fat_sdcard_unmount(mount_point, card);
         SD_CS(1);
     }

     mount_ret = ESP_FAIL;
     card = NULL;
     sd_saved_fs = NULL;
     sd_owner = SD_OWNER_NONE;
     sd_local_mounted = false;
     sd_recovery_required = false;
     SD_CS(1);
     sd_unlock();
 }

 esp_err_t sd_card_check(void)
 {
     if (!sd_access_begin(portMAX_DELAY))
     {
         return ESP_ERR_INVALID_STATE;
     }

     esp_err_t check_ret = sdmmc_get_status(card);
     sd_access_end();

     return check_ret;
 }

 bool sd_access_begin(TickType_t timeout)
 {
     if (!sd_lock(timeout))
     {
         return false;
     }

     if (sd_owner != SD_OWNER_LOCAL || !sd_local_mounted || card == NULL)
     {
         sd_unlock();
         return false;
     }

     SD_CS(0);
     return true;
 }

 void sd_access_end(void)
 {
     SD_CS(1);
     sd_unlock();
 }

 bool sd_usb_access_begin(TickType_t timeout)
 {
     if (!sd_lock(timeout))
     {
         return false;
     }

     if (sd_owner != SD_OWNER_USB || card == NULL)
     {
         sd_unlock();
         return false;
     }

     SD_CS(0);
     return true;
 }

 void sd_usb_access_end(void)
 {
     SD_CS(1);
     sd_unlock();
 }

 bool sd_local_session_begin(void)
 {
     if (!sd_lock(portMAX_DELAY))
     {
         return false;
     }

     bool allowed = sd_owner == SD_OWNER_LOCAL && sd_local_mounted && card != NULL &&
                    !sd_recovery_required && sd_local_sessions < UINT32_MAX;
     if (allowed)
     {
         sd_local_sessions++;
     }

     sd_unlock();
     return allowed;
 }

 void sd_local_session_end(void)
 {
     sd_local_session_finish(false);
 }

 esp_err_t sd_usb_take_ownership(void)
 {
     if (!sd_lock(portMAX_DELAY))
     {
         return ESP_ERR_INVALID_STATE;
     }

     if (sd_owner == SD_OWNER_USB)
     {
         sd_unlock();
         return ESP_OK;
     }

     if (sd_owner != SD_OWNER_LOCAL || !sd_local_mounted || card == NULL ||
         sd_recovery_required || sd_local_sessions != 0)
     {
         sd_unlock();
         return ESP_ERR_INVALID_STATE;
     }

     DWORD free_clusters = 0;
     FATFS *fs = NULL;
     SD_CS(0);
     FRESULT fs_result = f_getfree("0:", &free_clusters, &fs);
     if (fs_result == FR_OK && disk_ioctl(0, CTRL_SYNC, NULL) != RES_OK)
     {
         fs_result = FR_DISK_ERR;
     }
     if (fs_result == FR_OK)
     {
         fs_result = f_mount(NULL, "0:", 0);
     }
     SD_CS(1);

     if (fs_result != FR_OK || fs == NULL)
     {
         sd_unlock();
         return ESP_FAIL;
     }

     sd_saved_fs = fs;
     sd_local_mounted = false;
     sd_owner = SD_OWNER_USB;
     sd_unlock();
     return ESP_OK;
 }

 esp_err_t sd_usb_release_ownership(void)
 {
     if (!sd_lock(portMAX_DELAY))
     {
         return ESP_ERR_INVALID_STATE;
     }

     if (sd_owner != SD_OWNER_USB)
     {
         sd_unlock();
         return ESP_OK;
     }

     SD_CS(0);
     FRESULT fs_result = FR_NOT_ENABLED;
     if (sd_saved_fs != NULL && disk_ioctl(0, CTRL_SYNC, NULL) == RES_OK)
     {
         fs_result = f_mount(sd_saved_fs, "0:", 1);
     }
     SD_CS(1);

     if (fs_result == FR_OK)
     {
         sd_owner = SD_OWNER_LOCAL;
         sd_local_mounted = true;
     }
     else
     {
         sd_owner = SD_OWNER_NONE;
         sd_local_mounted = false;
     }

     sd_unlock();
     return fs_result == FR_OK ? ESP_OK : ESP_FAIL;
 }

 bool sd_usb_has_ownership(void)
 {
     if (!sd_lock(portMAX_DELAY))
     {
         return false;
     }

     bool owned = sd_owner == SD_OWNER_USB;
     sd_unlock();
     return owned;
 }

 FRESULT sd_f_open(FIL *fp, const TCHAR *path, BYTE mode)
 {
     if (!sd_local_session_begin())
     {
         return FR_NOT_READY;
     }
     if (!sd_access_begin(portMAX_DELAY))
     {
         sd_local_session_end();
         return FR_NOT_READY;
     }

     FRESULT result = f_open(fp, path, mode);
     sd_access_end();
     if (result != FR_OK)
     {
         sd_local_session_end();
     }
     return result;
 }

 FRESULT sd_f_close(FIL *fp)
 {
     FRESULT result = FR_NOT_READY;
     if (sd_access_begin(portMAX_DELAY))
     {
         result = f_close(fp);
         sd_access_end();
     }
     sd_local_session_finish(result != FR_OK);
     return result;
 }

 FRESULT sd_f_read(FIL *fp, void *buffer, UINT bytes_to_read, UINT *bytes_read)
 {
     if (!sd_access_begin(portMAX_DELAY))
     {
         if (bytes_read != NULL)
         {
             *bytes_read = 0;
         }
         return FR_NOT_READY;
     }

     FRESULT result = f_read(fp, buffer, bytes_to_read, bytes_read);
     sd_access_end();
     return result;
 }

 FRESULT sd_f_write(FIL *fp, const void *buffer, UINT bytes_to_write, UINT *bytes_written)
 {
     if (!sd_access_begin(portMAX_DELAY))
     {
         if (bytes_written != NULL)
         {
             *bytes_written = 0;
         }
         return FR_NOT_READY;
     }

     FRESULT result = f_write(fp, buffer, bytes_to_write, bytes_written);
     sd_access_end();
     return result;
 }

 FRESULT sd_f_lseek(FIL *fp, FSIZE_t offset)
 {
     if (!sd_access_begin(portMAX_DELAY))
     {
         return FR_NOT_READY;
     }

     FRESULT result = f_lseek(fp, offset);
     sd_access_end();
     return result;
 }

 FRESULT sd_f_sync(FIL *fp)
 {
     if (!sd_access_begin(portMAX_DELAY))
     {
         return FR_NOT_READY;
     }

     FRESULT result = f_sync(fp);
     sd_access_end();
     return result;
 }

 FRESULT sd_f_stat(const TCHAR *path, FILINFO *info)
 {
     if (!sd_access_begin(portMAX_DELAY))
     {
         return FR_NOT_READY;
     }

     FRESULT result = f_stat(path, info);
     sd_access_end();
     return result;
 }

 FRESULT sd_f_mkdir(const TCHAR *path)
 {
     if (!sd_access_begin(portMAX_DELAY))
     {
         return FR_NOT_READY;
     }

     FRESULT result = f_mkdir(path);
     sd_access_end();
     return result;
 }

 FRESULT sd_f_opendir(FF_DIR *dir, const TCHAR *path)
 {
     if (!sd_local_session_begin())
     {
         return FR_NOT_READY;
     }
     if (!sd_access_begin(portMAX_DELAY))
     {
         sd_local_session_end();
         return FR_NOT_READY;
     }

     FRESULT result = f_opendir(dir, path);
     sd_access_end();
     if (result != FR_OK)
     {
         sd_local_session_end();
     }
     return result;
 }

 FRESULT sd_f_readdir(FF_DIR *dir, FILINFO *info)
 {
     if (!sd_access_begin(portMAX_DELAY))
     {
         return FR_NOT_READY;
     }

     FRESULT result = f_readdir(dir, info);
     sd_access_end();
     return result;
 }

 FRESULT sd_f_closedir(FF_DIR *dir)
 {
     FRESULT result = FR_NOT_READY;
     if (sd_access_begin(portMAX_DELAY))
     {
         result = f_closedir(dir);
         sd_access_end();
     }
     sd_local_session_finish(result != FR_OK);
     return result;
 }

 bool lv_fs_fatfs_access_begin(void)
 {
     return sd_access_begin(portMAX_DELAY);
 }

 void lv_fs_fatfs_access_end(void)
 {
     sd_access_end();
 }

 bool lv_fs_fatfs_session_begin(void)
 {
     return sd_local_session_begin();
 }

 void lv_fs_fatfs_session_end(void)
 {
     sd_local_session_end();
 }

 FRESULT lv_fs_fatfs_file_close(FIL *fp)
 {
     return sd_f_close(fp);
 }
 
 /**
  * @brief       获取SD卡相关信息
  * @param       out_total_bytes：总大小
  * @param       out_free_bytes：剩余大小
  * @retval      无
  */
 void sd_get_fatfs_usage(size_t *out_total_bytes, size_t *out_free_bytes)
 {
     if (out_total_bytes != NULL)
     {
         *out_total_bytes = 0;
     }
     if (out_free_bytes != NULL)
     {
         *out_free_bytes = 0;
     }
     if (!sd_access_begin(portMAX_DELAY))
     {
         return;
     }

     FATFS *fs;
     DWORD free_clusters;
     FRESULT res = f_getfree("0:", &free_clusters, &fs);
     if (res != FR_OK || fs == NULL)
     {
         sd_access_end();
         return;
     }
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

     sd_access_end();
 }
