/**
 ******************************************************************************
 * @file        esp_rtc.h
 * @version     V1.0
 * @brief       RTC驱动代码
 *****************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

#ifndef __ESP_RTC_H
#define __ESP_RTC_H

#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <sys/time.h>
#include <stdbool.h>


/* 时间结构体, 包括年月日周时分秒等信息 */
typedef struct
{
    uint8_t hour;       /* 时 */
    uint8_t min;        /* 分 */
    uint8_t sec;        /* 秒 */
    /* 公历年月日周 */
    uint16_t year;      /* 年 */
    uint8_t  month;     /* 月 */
    uint8_t  date;      /* 日 */
    uint8_t  week;      /* 周 */
} _calendar_obj;

extern _calendar_obj calendar;      /* 时间结构体 */

/* 函数声明 */
void rtc_set_time(int year,int mon,int mday,int hour,int min,int sec);  /* 设置时间 */
void rtc_get_time(void);                                                /* 获取时间 */
uint8_t rtc_get_week(uint16_t year, uint8_t month, uint8_t day);        /* 获取周几 */
void rtc_time_init(void);
bool rtc_restore_saved_time(void);
bool rtc_save_current_time(void);
void rtc_start_sntp_sync(void);

#endif
