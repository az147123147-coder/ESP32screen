/**
 ******************************************************************************
 * @file        tud_sd.h
 * @version     V1.0
 * @brief       SD卡模拟U盘（USB）代码
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

#ifndef __TUD_SD_H
#define __TUD_SD_H

#include <inttypes.h>
#include "ff.h"
#include "diskio.h"
#include "esp_vfs_fat.h"
#include "tinyusb.h"
#include "esp_idf_version.h"
#include "spi_sd.h"


/* USB控制器 */
typedef struct
{
    uint8_t status;                         /* bit0:0,断开;1,连接 */
}__usbdev;

extern __usbdev g_usbdev;                   /* USB控制器 */

/* 函数声明 */
void tud_usb_sd(void);

#endif
