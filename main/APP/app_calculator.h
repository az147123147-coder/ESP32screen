/**
 ******************************************************************************
 * @file        app_calendar.h
 * @version     V1.0
 * @brief       LVGL 计算器 APP
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

#ifndef __APP_CALCULATOR_H__
#define __APP_CALCULATOR_H__

#include "lvgl.h"
#include <stdio.h>
#include <math.h>
#include "lv_main_ui.h"

/* 结构体声明 */
typedef struct {
    lv_obj_t * calculator_main_ui;   /* 计算器UI容器 */
    lv_obj_t * calculator_btnm;      /* 计算器的矩阵按键 */
    lv_obj_t * calculator_text_area; /* 计算器的输入文本区域 */
    lv_obj_t * calculator_dec;       /* 计算描述 */
    lv_obj_t * calculator_result;    /* 计算结果 */
} calculator_ui_t;

extern calculator_ui_t calculator_ui;

/* 函数声明 */
void lv_app_calculator_init(void);
void lv_calculator_del(void);

#endif