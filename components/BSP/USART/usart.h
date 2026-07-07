/**
 ******************************************************************************
 * @file        usart.h
 * @version     V1.0
 * @brief       串口初始化代码
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

 #ifndef _USART_H
 #define _USART_H
 
 #include <string.h>
 #include "freertos/FreeRTOS.h"
 #include "freertos/task.h"
 #include "driver/uart.h"
 #include "driver/uart_select.h"
 #include "driver/gpio.h"
 
 
 /* 引脚和串口定义 */
 #define USART_UX            UART_NUM_0
 #define USART_TX_GPIO_PIN   GPIO_NUM_43
 #define USART_RX_GPIO_PIN   GPIO_NUM_44
 
 /* 串口接收相关定义 */
 #define RX_BUF_SIZE         1024    /* 环形缓冲区大小 */
 
 /* 函数声明 */
 void usart_init(uint32_t baudrate); /* 初始化串口 */
 
 #endif
 