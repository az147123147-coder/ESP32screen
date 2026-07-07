/**
 ******************************************************************************
 * @file        app_calendar.c
 * @version     V1.0
 * @brief       LVGL 日历 APP
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

#include "app_calendar.h"
#include "esp_rtc.h"
#include <stdbool.h>


calendar_ui_t lv_calendar_ui;

static lv_calendar_date_t highlight_days[1]; /* 定义的日期,必须用全局或静态定义 */
// static const char * years = "2030\n2029\n2028\n2027\n2026\n2025\n2024";
extern lv_obj_t * back_btn;

static void app_calendar_apply_current_date(bool force_update)
{
    if (lv_calendar_ui.calendar_obj == NULL || !lv_obj_is_valid(lv_calendar_ui.calendar_obj))
    {
        return;
    }

    rtc_get_time();
    uint16_t year = calendar.year;
    uint8_t month = calendar.month;
    uint8_t day = calendar.date;

    if (year < 2024 || month < 1 || month > 12 || day < 1 || day > 31)
    {
        year = 2024;
        month = 1;
        day = 1;
    }

    if (!force_update &&
        lv_calendar_ui.current_year == year &&
        lv_calendar_ui.current_month == month &&
        lv_calendar_ui.current_day == day)
    {
        return;
    }

    lv_calendar_ui.current_year = year;
    lv_calendar_ui.current_month = month;
    lv_calendar_ui.current_day = day;

    lv_calendar_set_today_date(lv_calendar_ui.calendar_obj, year, month, day);
    lv_calendar_set_showed_date(lv_calendar_ui.calendar_obj, year, month);

    highlight_days[0].year = year;
    highlight_days[0].month = month;
    highlight_days[0].day = day;

    lv_calendar_set_highlighted_dates(lv_calendar_ui.calendar_obj, highlight_days, 1);
    lv_obj_update_layout(lv_calendar_ui.calendar_obj);
}

static void app_calendar_refresh_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    app_calendar_apply_current_date(false);
}


void app_calendar_ui_init(void)
{
    lv_calendar_ui.current_year = 0;
    lv_calendar_ui.current_month = 0;
    lv_calendar_ui.current_day = 0;

    lv_calendar_ui.calendar_main_ui = lv_obj_create(lv_scr_act());
    lv_obj_set_style_bg_color(lv_calendar_ui.calendar_main_ui, lv_color_hex(0x000000), LV_STATE_DEFAULT);                  /* 设置背景颜色 */
    lv_obj_set_size(lv_calendar_ui.calendar_main_ui, lv_obj_get_width(lv_scr_act()), lv_obj_get_height(lv_scr_act()) - lv_obj_get_height(lv_scr_act()) / 20);  /* 设置容器大小 */
    lv_obj_set_style_radius(lv_calendar_ui.calendar_main_ui, 0, LV_STATE_DEFAULT);                                         /* 无圆角 */
    lv_obj_set_style_border_opa(lv_calendar_ui.calendar_main_ui, LV_OPA_0, LV_STATE_DEFAULT);                              /* 边界透明 */
    lv_obj_set_pos(lv_calendar_ui.calendar_main_ui, 0, lv_obj_get_height(lv_scr_act()) / 20);                              /* 设置位置 */
    lv_obj_clear_flag(lv_calendar_ui.calendar_main_ui, LV_OBJ_FLAG_SCROLLABLE);                                            /* 禁止滚动 */
    lv_obj_update_layout(lv_calendar_ui.calendar_main_ui);

    /* 定义并初始化日历 */
    lv_calendar_ui.calendar_obj = lv_calendar_create(lv_calendar_ui.calendar_main_ui);
    /* 设置日历的大小 */
    lv_obj_set_size(lv_calendar_ui.calendar_obj, lv_obj_get_width(lv_calendar_ui.calendar_main_ui) ,lv_obj_get_height(lv_calendar_ui.calendar_main_ui));
    lv_obj_set_style_radius(lv_calendar_ui.calendar_obj, 0, LV_STATE_DEFAULT);                                         /* 无圆角 */
    lv_obj_set_style_border_opa(lv_calendar_ui.calendar_obj, LV_OPA_0, LV_STATE_DEFAULT);                              /* 边界透明 */
    lv_obj_center(lv_calendar_ui.calendar_obj);

    /* 设置日历头 */
    lv_calendar_header_arrow_create(lv_calendar_ui.calendar_obj);

    app_calendar_apply_current_date(true);
    lv_calendar_ui.refresh_timer = lv_timer_create(app_calendar_refresh_timer_cb, 1000, NULL);

	/* 隐藏box */
	lv_hidden_box();

	lv_obj_move_foreground(back_btn);
	app_obj_general.del_parent = lv_calendar_ui.calendar_main_ui;
	app_obj_general.APP_Function = lv_calendar_del;
	app_obj_general.app_state = NOT_DEL_STATE;

    //lv_general.current_parent = lv_calendar_ui.calendar_main_ui;
}

/**
 * @brief       计算器界面退出
 * @param       无
 * @retval      无
 */
void lv_calendar_del(void)
{
    if (lv_calendar_ui.refresh_timer != NULL)
    {
        lv_timer_del(lv_calendar_ui.refresh_timer);
        lv_calendar_ui.refresh_timer = NULL;
    }

    /* 删除计算器父类 */
    if (lv_calendar_ui.calendar_main_ui != NULL && lv_obj_is_valid(lv_calendar_ui.calendar_main_ui))
    {
        lv_obj_del(lv_calendar_ui.calendar_main_ui);
    }

    lv_calendar_ui.calendar_main_ui = NULL;
    lv_calendar_ui.calendar_obj = NULL;
    /* 显示主界面 */
    lv_display_box();
}
