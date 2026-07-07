/**
 ******************************************************************************
 * @file        app_brush.c
 * @version     V1.0
 * @brief       LVGL 画笔 APP
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

#ifndef __APP_BRUSH_H__
#define __APP_BRUSH_H__

#include "lvgl.h"
#include <stdio.h>
#include <math.h>
#include "lv_main_ui.h"
#include "lvgl_demo.h"
#include "lcd.h"

typedef struct
{
    lv_obj_t * lv_touch_cont;
    lv_obj_t * box_cont;
    lv_obj_t * box_label_pen;
    lv_obj_t * box_eraser_cont;
    lv_obj_t * box_label_eraser;
    lv_obj_t * box_label_color;
    lv_obj_t * box_colorwheel;
    lv_obj_t * box_slider;
    lv_obj_t * box_slider_label;
    lv_obj_t *box_sound_label;
    lv_point_t last_point;
    lv_point_t current_point; 
    uint8_t touch_pen_size;
    lv_color_t pen_color;
    lv_color_t * cbuf;
    lv_timer_t * touch_timer;
}touch_ui_t;

extern touch_ui_t lv_touch;

/* 函数声明 */
void lv_app_brush_init(void);
void lv_brush_del(void);

#endif