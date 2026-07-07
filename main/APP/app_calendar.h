/**
 ******************************************************************************
 * @file        app_calendar.h
 * @version     V1.0
 * @brief       LVGL 日历 APP
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

#ifndef __APP_CALENDAR_H__
#define __APP_CALENDAR_H__

#include "lvgl.h"
#include <stdio.h>
#include <math.h>
#include "lv_main_ui.h"
#include "esp_rtc.h"


/* 结构体声明 */
typedef struct {
    lv_obj_t * calendar_main_ui;    /* 日历UI容器 */
    lv_obj_t * calendar_obj;        /* 日历控件 */
    lv_obj_t * calendar_msgbox;     /* 设置日历 */
    lv_timer_t * refresh_timer;
    uint16_t current_year;
    uint8_t current_month;
    uint8_t current_day;
} calendar_ui_t;

extern calendar_ui_t lv_calendar_ui;

/* 函数声明 */
void app_calendar_ui_init(void);
void lv_calendar_del(void);

#endif
