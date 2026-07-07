/**
 ******************************************************************************
 * @file        lv_start_ui.h
 * @version     V1.0
 * @brief       LVGL 开机Ui
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

#ifndef __LV_START_UI_H
#define __LV_START_UI_H

#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lv_main_ui.h"
#include <math.h>


 /* 开机界面所需的控件 */
 typedef struct {
	lv_obj_t *logo_box;
	lv_obj_t *logo_obj;          /* 第一个图片对象 */
	lv_obj_t *logo1_obj;         /* 第二个图片对象 */
	lv_coord_t img_w;            /* 第一个图片原始宽度 */
	lv_coord_t img_h;            /* 第一个图片原始高度 */
	lv_coord_t img1_w;           /* 第二个图片原始宽度 */
	lv_coord_t img1_h;           /* 第二个图片原始高度 */
} lv_starting_obj_t;

/* 函数声明 */
void lv_start_ui(void);

#endif
