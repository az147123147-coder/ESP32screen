/**
 ******************************************************************************
 * @file        my_spi.h
 * @version     V1.0
 * @brief       SPI驱动代码
 *****************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

#ifndef __MY_SPI_H
#define __MY_SPI_H

#include <unistd.h>
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_err.h"

/* SPI驱动管脚 */
#define SPI_SCLK_PIN        GPIO_NUM_47
#define SPI_MOSI_PIN        GPIO_NUM_21
#define SPI_MISO_PIN        GPIO_NUM_48

/* SPI端口 */
#define MY_SPI_HOST         SPI2_HOST
/* 设备句柄 */
extern spi_device_handle_t MY_SD_Handle;	/* SD卡句柄 */

/* 函数声明 */
esp_err_t my_spi_init(void);    			/* SPI初始化 */
esp_err_t my_spi_clock_bytes(size_t byte_count);
#endif
