/**
 ******************************************************************************
 * @file        xl9555.h
 * @version     V1.0
 * @brief       XL9555驱动代码
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

#ifndef __XL9555_H
#define __XL9555_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "myiic.h"
#include "string.h"


/* XL9555寄存器宏 */
#define XL9555_INPUT_PORT0_REG      0                               /* 输入寄存器0地址 */
#define XL9555_INPUT_PORT1_REG      1                               /* 输入寄存器1地址 */
#define XL9555_OUTPUT_PORT0_REG     2                               /* 输出寄存器0地址 */
#define XL9555_OUTPUT_PORT1_REG     3                               /* 输出寄存器1地址 */
#define XL9555_INVERSION_PORT0_REG  4                               /* 极性反转寄存器0地址 */
#define XL9555_INVERSION_PORT1_REG  5                               /* 极性反转寄存器1地址 */
#define XL9555_CONFIG_PORT0_REG     6                               /* 方向配置寄存器0地址 */
#define XL9555_CONFIG_PORT1_REG     7                               /* 方向配置寄存器1地址 */

#define XL9555_ADDR                 0X20                            /* XL9555器件7位地址-->请看手册（9.1. Device Address） */

/* XL9555各个IO的功能 */
#define TP_RST_IO                   0x0001
#define LCD_RST_IO                  0x0002
#define SD_CS_IO                    0x0004
#define BL_CTR_IO                   0x0008
#define LED1_IO                     0x0010
#define IO5                         0x0020
#define IO6                         0x0040
#define IO7                         0x0080
#define IO8                         0x0100
#define IO9                         0x0200
#define IO10                        0x0400
#define IO11                        0x0800
#define IO12                        0x1000
#define IO13                        0x2000
#define IO14                        0x4000
#define IO15                        0x8000

/* LED1端口定义 */
#define LED1(x)          do { x ?                                      \
								xl9555_pin_write(LED1_IO, 1) :  \
								xl9555_pin_write(LED1_IO, 0); \
							} while(0)   

/* LED1翻转 */
#define LED1_TOGGLE()  	 xl9555_pin_toggle(LED1_IO);						

/* 函数声明 */
esp_err_t xl9555_init(void);                                            /* 初始化XL9555 */
int xl9555_pin_read(uint16_t pin);                                      /* 获取某个IO状态 */
uint16_t xl9555_pin_write(uint16_t pin, int val);                       /* 控制某个IO的电平 */
esp_err_t xl9555_read_byte(uint8_t* data, size_t len);                  /* 读取XL9555的IO值 */
esp_err_t xl9555_write_byte(uint8_t reg, uint8_t *data, size_t len);    /* 向XL9555寄存器写入数据 */
esp_err_t xl9555_pin_toggle(uint16_t pin);                              /* 翻转某个io的电平 */

#endif
