/**
 ******************************************************************************
 * @file        app_usb.h
 * @version     V1.0
 * @brief       LVGL USB APP
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

#ifndef __APP_USB_H__
#define __APP_USB_H__

#include "lvgl.h"
#include <stdio.h>
#include <math.h>
#include "lv_main_ui.h"
#include "lvgl_demo.h"
#include "lcd.h"
#include "tud_sd.h"


/* 结构体声明 */
typedef struct {
    lv_obj_t* main_ui;
    lv_obj_t* usb_img;
	lv_obj_t* status_label;  
    lv_timer_t* check_timer;
} usb_ui_t;

/* 函数声明 */
void app_usb_ui_init(void);
void lv_usb_del(void);

#endif
