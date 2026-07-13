/**
 ******************************************************************************
 * @file        lvgl_demo.h
 * @version     V1.0
 * @brief       lvgl_demo
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */
 
#ifndef __LVGL_DEMO_H
#define __LVGL_DEMO_H

#include "lcd.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "sdmmc_cmd.h"
#include "spi_sd.h"
#include "touch.h"
#include "lv_start_ui.h"
#include "lv_main_ui.h"
#include "key.h"


extern lv_disp_drv_t disp_drv;      /* 回调函数的参数 */
extern lv_indev_data_t touch_data;

/* 函数声明 */
esp_err_t lvgl_demo(void);
void lvgl_set_display_dir(uint8_t dir, bool flipped);
void lvgl_mux_unlock(void);
bool lvgl_mux_lock(int timeout_ms);
void touchpad_get_xy(lv_coord_t *x, lv_coord_t *y);
bool lvgl_notify_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx);
#endif
