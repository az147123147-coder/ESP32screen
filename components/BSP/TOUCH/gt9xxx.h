/**
 ******************************************************************************
 * @file        gt9xxx.h
 * @version     V1.0
 * @brief       电容触摸屏-GT9xxx 驱动代码
 *   @note      GT系列电容触摸屏IC通用驱动,本代码支持: GT9147/GT911/GT928/GT1151/GT9271 等多种
 *              驱动IC, 这些驱动IC仅ID不一样, 具体代码基本不需要做任何修改即可通过本代码直接驱动
 *
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

#ifndef __GT9XXX_H
#define __GT9XXX_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "touch.h"
#include "string.h"
#include "myiic.h"
#include "xl9555.h"
#include "lcd.h"

/* 触摸芯片引脚相关定义 */
#define GT9XXX_INT_GPIO_PIN             GPIO_NUM_42

#define GT9XXX_INT                      gpio_get_level(GT9XXX_INT_GPIO_PIN)

/* 触摸屏复位引脚 */
#define CT_RST(x)       do { x ?                              \
                            xl9555_pin_write(TP_RST_IO, 1):   \
                            xl9555_pin_write(TP_RST_IO, 0);   \
                        } while(0)

/* GT器件地址及IIC读写命令 */
#define GT9XXX_DEV_ID                   0x14        /* GTXXX设备地址 */
#define GT9XXX_CMD_WR                   0X28        /* 写命令 */
#define GT9XXX_CMD_RD                   0X29        /* 读命令 */

/* GT9XXX 部分寄存器定义  */
#define GT9XXX_CTRL_REG                 0X8040      /* GT9XXX控制寄存器 */
#define GT9XXX_CFGS_REG                 0X8047      /* GT9XXX配置起始地址寄存器 */
#define GT9XXX_CHECK_REG                0X80FF      /* GT9XXX校验和寄存器 */
#define GT9XXX_PID_REG                  0X8140      /* GT9XXX产品ID寄存器 */

#define GT9XXX_GSTID_REG                0X814E      /* GT9XXX当前检测到的触摸情况 */
#define GT9XXX_TP1_REG                  0X8150      /* 第一个触摸点数据地址 */
#define GT9XXX_TP2_REG                  0X8158      /* 第二个触摸点数据地址 */
#define GT9XXX_TP3_REG                  0X8160      /* 第三个触摸点数据地址 */
#define GT9XXX_TP4_REG                  0X8168      /* 第四个触摸点数据地址 */
#define GT9XXX_TP5_REG                  0X8170      /* 第五个触摸点数据地址 */
#define GT9XXX_TP6_REG                  0X8178      /* 第六个触摸点数据地址 */
#define GT9XXX_TP7_REG                  0X8180      /* 第七个触摸点数据地址 */
#define GT9XXX_TP8_REG                  0X8188      /* 第八个触摸点数据地址 */
#define GT9XXX_TP9_REG                  0X8190      /* 第九个触摸点数据地址 */
#define GT9XXX_TP10_REG                 0X8198      /* 第十个触摸点数据地址 */

/* 函数声明 */
esp_err_t gt9xxx_init(void);                                        /* 初始化gt9xxx触摸屏 */
uint8_t gt9xxx_scan(uint8_t mode);                                  /* 扫描触摸屏 */
esp_err_t gt9xxx_wr_reg(uint16_t reg, uint8_t *buf, uint8_t len);   /* 向gt9xxx写入数据 */
esp_err_t gt9xxx_rd_reg(uint16_t reg, uint8_t *buf, uint8_t len);   /* 从gt9xxx读出数据 */

#endif
