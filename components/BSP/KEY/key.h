/**
 ******************************************************************************
 * @file        key.h
 * @version     V1.0
 * @brief       按键驱动代码
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

#ifndef __KEY_H_
#define __KEY_H_

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"


/* 引脚定义 */
#define BOOT_GPIO_PIN   GPIO_NUM_0

/*IO操作*/
#define BOOT            gpio_get_level(BOOT_GPIO_PIN)

/* 按键按下定义 */
#define BOOT_PRES       1       /* BOOT按键按下 */

/* 函数声明 */
void key_init(void);            /* 初始化按键 */
uint8_t key_scan(uint8_t mode); /* 按键扫描函数 */

#endif
