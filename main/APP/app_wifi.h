/**
 ******************************************************************************
 * @file        app_wifi.h
 * @version     V1.0
 * @brief       LVGL WiFi APP
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

#ifndef __WIFI_APP_H
#define __WIFI_APP_H

#include "lvgl.h"
#include "lv_main_ui.h"

/* 结构体声明 */
typedef struct {
    lv_obj_t *wifi_main_ui;
    lv_obj_t *list;
    lv_obj_t *scan_btn;
    lv_obj_t *back_btn;
    lv_obj_t *conn_box;
    lv_obj_t *pwd_ta;
	lv_obj_t *status_bar;
	lv_obj_t *status_label;
	lv_obj_t *status_icon;
    uint8_t initialized;
} wifi_ui_t;

/* 函数声明 */
void wifi_app_init(void);
void wifi_app_del(void);

#endif