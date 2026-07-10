/**
 ******************************************************************************
 * @file        main.c
 * @version     V1.0
 * @brief       LVGL 综合实验
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3
 ******************************************************************************
 */

#include "nvs_flash.h"
#include "myiic.h"
#include "xl9555.h"
#include "lvgl_demo.h"
#include "my_spi.h"
#include "key.h"
#include "exfuns.h"
#include "esp_rtc.h"
#include "esp_wifi.h"
#include "usart.h"


/**
 * @brief       程序入口
 * @param       无
 * @retval      无
 */
void app_main(void)
{
    esp_err_t ret;
    ret = nvs_flash_init();             /* 初始化NVS */

    lcd_cfg_t lcd_config_info = {0};
    lcd_config_info.notify_flush_ready = lvgl_notify_flush_ready;
    lcd_config_info.user_ctx = &disp_drv;

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    myiic_init();                       /* 初始化IIC0 */
    my_spi_init();                      /* 初始化SPI */
    key_init();                         /* 初始化KEY */
    xl9555_init();                      /* 初始化IO扩展芯片 */
	usart_init(115200);                 /* 初始化串口0 */ 
    rtc_time_init();
    rtc_restore_saved_time();
    lcd_init(lcd_config_info);          /* 初始化LCD */	
    tud_usb_sd();                       /* 初始化USB硬件 */
    lvgl_demo();                        /* UI界面程序入口 */
}
