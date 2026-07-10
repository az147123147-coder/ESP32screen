/**
 ******************************************************************************
 * @file        lv_main_ui.h
 * @version     V1.0
 * @brief       LVGL 综合实验
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

#ifndef LV_MAIN_UI_H
#define LV_MAIN_UI_H

#include "lvgl.h"
#include "spi_sd.h"
#include "esp_rtc.h"
#include "lvgl_demo.h"
#include "lv_photo_ui.h"
#include "lv_video_ui.h"
#include "app_brush.h"
#include "app_calculator.h"
#include "app_calendar.h"
#include "app_file.h"
#include "app_usb.h"
#include "app_wifi.h"
#include "xiaoxiaole.h"
#include "demos/lv_demos.h"

/* APP ICO number */
#define main_ic0_num    9

/* 定义描述APP信息的结构体 */
typedef struct
{
    uint8_t  app_id;            /* app ID */
    char * app_text_en;         /* app 英文名称 */
    const lv_img_dsc_t * icon_image;  /* app 图标源 */
}app_info_t;

typedef enum
{
    NOT_STATE = 0x0,
    WIFI_STATE = 0x1,
    TF_STATE = 0x2,
    USB_STATE = 0x4,
    VOICE_STATE = 0x8,
    CELL_STATE = 0x10,
}small_icon_state_t;

typedef struct {
    lv_obj_t *mian_box;
    /* 主体部分 */
    struct{
        lv_obj_t *main_mini_obx;
        lv_obj_t *mian_imagebg_obx;
        lv_obj_t *mian_time_text;
        lv_obj_t *ico[main_ic0_num];
    }mian_inter;

    // /* 小主体部分 */
    // struct{
    //     lv_obj_t *wifi;
    //     lv_obj_t *tf;
    //     lv_obj_t *usb;
    //     lv_obj_t *time;
    //     lv_obj_t *vioce;
    //     lv_obj_t *cell;
    // }mini_box;

   struct
   {
        lv_timer_t* lv_rtc_timer;
        uint8_t hour;
        uint8_t minute;
        uint8_t second;
        uint8_t year;
        uint8_t month;
        uint8_t date;
        uint8_t week;
        char rtc_tbuf[40];
   }rtc;

   small_icon_state_t small_icon_state;

}lv_main_ui_t;

typedef enum
{
    NOT_DEL_STATE = 0x0,
    DEL_STATE = 0x1,
}del_app_t;

/* 提示框架结构体 */
typedef struct
{
    lv_obj_t* current_parent;
    lv_obj_t* hidden_parent;
    lv_obj_t* del_parent;
    del_app_t app_state;
    void (*Function)(void);
    void (*APP_Function)(void);
    volatile uint8_t requires_sd;
} lv_m_general_t;

extern lv_m_general_t app_obj_general;

/* 函数声明 */
void lv_mian_ui(void);
void lv_msgbox(char *name);
void lv_sd_toast(char *name);
void lv_hidden_box(void);
void lv_display_box(void);
void lv_smail_icon_add_state(small_icon_state_t state);
void lv_smail_icon_clear_state(small_icon_state_t state);
uint8_t lv_smail_icon_get_state(small_icon_state_t state);
void lv_focus_restore(void);
void lv_mem_log(void);
#endif
