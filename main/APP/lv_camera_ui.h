/**
 ******************************************************************************
 * @file        lv_camera_ui.h
 * @version     V1.0
 * @brief       LVGL 相机 APP
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

#ifndef __LV_CAMERA_UI_H
#define __LV_CAMERA_UI_H

#include "lvgl.h"
#include "lcd.h"
#include "ppbuffer.h"
#include "usb_stream.h"
#include "esp_jpeg_dec.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "lv_main_ui.h"
#include "exfuns.h"
#include "lv_photo_ui.h"
#include "xl9555.h"

typedef struct 
{
    lv_obj_t *usb_camera_box;
    uint8_t usb_state;
    uint8_t usb_start;
    struct 
    {
        lv_obj_t *usb_test;
        lv_obj_t *camera_header;
        lv_obj_t *usb_pho_btn;
    }camera_buf;

}lv_usb_camera;

/* 函数申明 */
void usb_camera_ui(void);
esp_err_t usb_camera_init(void);
void lv_camera_del(void);
#endif
