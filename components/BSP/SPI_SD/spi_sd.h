/**
 ******************************************************************************
 * @file        spi_sd.h
 * @version     V1.0
 * @brief       SD卡 驱动代码
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

 #ifndef __SPI_SD_H
 #define __SPI_SD_H
 
 #include <unistd.h>
 #include <stdbool.h>
 #include "esp_vfs_fat.h"
 #include "driver/sdspi_host.h"
 #include "driver/spi_common.h"
 #include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "freertos/semphr.h"
#include "diskio.h"
 #include "my_spi.h"
 #include "xl9555.h"
 
 
 extern sdmmc_card_t *card;  /* 声明全局变量 card */
 
 /* 引脚定义 */
 #define MOUNT_POINT     "/0:"

 /* SD卡片选引脚 */		
#define	SD_CS(x)   do{ x ? \
	xl9555_pin_write(SD_CS_IO, 1):     \
	xl9555_pin_write(SD_CS_IO, 0);     \
}while(0)   
 
 /* 函数声明 */
 esp_err_t sd_spi_init(void);    /* SD卡初始化 */
bool sd_card_is_mounted(void);
bool sd_card_is_available(void);
esp_err_t sd_card_mount(void);
void sd_card_unmount(void);
esp_err_t sd_card_check(void);
void sd_get_fatfs_usage(size_t *out_total_bytes, size_t *out_free_bytes);
bool sd_access_begin(TickType_t timeout);
void sd_access_end(void);
bool sd_usb_access_begin(TickType_t timeout);
void sd_usb_access_end(void);
bool sd_local_session_begin(void);
void sd_local_session_end(void);
esp_err_t sd_usb_take_ownership(void);
esp_err_t sd_usb_release_ownership(void);
bool sd_usb_has_ownership(void);
FRESULT sd_f_open(FIL *fp, const TCHAR *path, BYTE mode);
FRESULT sd_f_close(FIL *fp);
FRESULT sd_f_read(FIL *fp, void *buffer, UINT bytes_to_read, UINT *bytes_read);
FRESULT sd_f_write(FIL *fp, const void *buffer, UINT bytes_to_write, UINT *bytes_written);
FRESULT sd_f_lseek(FIL *fp, FSIZE_t offset);
FRESULT sd_f_sync(FIL *fp);
FRESULT sd_f_stat(const TCHAR *path, FILINFO *info);
FRESULT sd_f_mkdir(const TCHAR *path);
FRESULT sd_f_opendir(FF_DIR *dir, const TCHAR *path);
FRESULT sd_f_readdir(FF_DIR *dir, FILINFO *info);
FRESULT sd_f_closedir(FF_DIR *dir);
#endif
