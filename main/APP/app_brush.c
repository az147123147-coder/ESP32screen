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

#include "app_brush.h"
#include "esp_heap_caps.h"
#include "esp_log.h"


static uint8_t box_pen_state = 0;
static uint8_t box_pen_color_state = 0;
touch_ui_t lv_touch;
extern lv_obj_t * back_btn;
uint16_t width = 320;         /* 画布宽度*/
uint16_t height = 320;        /* 画布高度*/

/**
 * @brief       触摸屏定时器任务
 * @param       task：定时器
 * @retval      无
 */
void lv_touch_task(lv_timer_t *task)
{
	/* 获取画布在屏幕上的绝对坐标 */ 
    lv_area_t canvas_coords;
    lv_obj_get_coords(lv_touch.lv_touch_cont, &canvas_coords);

    if (touch_data.state == LV_INDEV_STATE_PR)  /* 触摸点按下 */ 
    {
		/* 使用画布坐标转换 */ 
        int x = touch_data.point.x - canvas_coords.x1;
        int y = touch_data.point.y - canvas_coords.y1;

        /* 判断触摸点是否在画布区域内 */ 
        if (x >= 0 && x < width && y >= 0 && y < height)
        {
            if (lv_touch.last_point.x == -1 || lv_touch.last_point.y == -1)
            {
                /* 初始化 last_point 为触摸的第一个点 */ 
                lv_touch.last_point.x = x;
                lv_touch.last_point.y = y;
            }

            lv_touch.current_point.x = x;   
            lv_touch.current_point.y = y;

            lv_draw_line_dsc_t line_dsc1;
			lv_draw_line_dsc_init(&line_dsc1);
            line_dsc1.color = lv_touch.pen_color;       
            line_dsc1.width = lv_touch.touch_pen_size;  
            line_dsc1.opa = LV_OPA_COVER;               
            line_dsc1.round_start = 1;                  
            line_dsc1.round_end = 1;                   

            lv_canvas_draw_line(lv_touch.lv_touch_cont, (lv_point_t[]) {lv_touch.last_point, lv_touch.current_point}, 2, &line_dsc1);
            lv_obj_invalidate(lv_touch.lv_touch_cont);

            lv_touch.last_point = lv_touch.current_point;
        }
    }
    else
    {
        lv_touch.last_point.x = -1;  
        lv_touch.last_point.y = -1;
    }
}

/**
 * @brief       触摸屏回调
 * @retval      无
 */
static void touch_event_cb(lv_event_t *e)
{
    lv_obj_t *target = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);


    if (code == LV_EVENT_CLICKED)
    {
        //lv_obj_remove_event_cb(lv_scr_act(),lv_scr_event_cb);
        if (target == lv_touch.box_label_eraser)
        {
           lv_canvas_fill_bg(lv_touch.lv_touch_cont, lv_color_hex(0xFFFFFF), LV_OPA_COVER);
        }
        else if (target == lv_touch.box_label_pen)
        {
            if (box_pen_state == 0)
            {
                lv_obj_clear_flag(lv_touch.box_slider, LV_OBJ_FLAG_HIDDEN);        /* 清除对象标志 */
                lv_obj_clear_flag(lv_touch.box_slider_label, LV_OBJ_FLAG_HIDDEN);  /* 清除对象标志 */
                lv_obj_clear_flag(lv_touch.box_sound_label, LV_OBJ_FLAG_HIDDEN);   /* 清除对象标志 */
                box_pen_state = 1;
            }
        }
        else if (target == lv_touch.box_label_color)
        {
            if (box_pen_color_state == 0)
            {
                lv_obj_clear_flag(lv_touch.box_colorwheel, LV_OBJ_FLAG_HIDDEN);   /* 清除对象标志 */
                box_pen_color_state = 1;
            }
        }
    }
    else if (code == LV_EVENT_VALUE_CHANGED)
    {
        if (target == lv_touch.box_slider)
        {
            lv_label_set_text_fmt(lv_touch.box_slider_label, "%d",(int)lv_slider_get_value(target));
        }
    }
    else if (code == LV_EVENT_RELEASED)
    {
        if (target == lv_touch.box_slider)
        {
            lv_touch.touch_pen_size = lv_slider_get_value(target);           /* 获取滑块当前值 */
            lv_obj_add_flag(lv_touch.box_slider, LV_OBJ_FLAG_HIDDEN);        /* 添加对象标志 */
            lv_obj_add_flag(lv_touch.box_slider_label, LV_OBJ_FLAG_HIDDEN);  /* 添加对象标志 */
            lv_obj_add_flag(lv_touch.box_sound_label, LV_OBJ_FLAG_HIDDEN);   /* 添加对象标志 */
            box_pen_state = 0;
        }
        else if (target == lv_touch.box_colorwheel)
        {
            lv_touch.pen_color = lv_colorwheel_get_rgb(target);  
            lv_obj_add_flag(lv_touch.box_colorwheel, LV_OBJ_FLAG_HIDDEN);   /* 添加对象标志 */
            box_pen_color_state = 0; 
        }
		
		//lv_obj_add_event_cb(lv_scr_act(), lv_scr_event_cb, LV_EVENT_ALL, NULL);
    }
}

/**
 * @brief       初始化手绘板并创建定时器任务
 * @param       无
 * @retval      无
 */
void lv_app_brush_init(void)
{
	width = 320;
    height = 320;

    lv_touch.touch_pen_size = 3;
    lv_touch.pen_color = _LV_COLOR_MAKE_TYPE_HELPER LV_COLOR_MAKE(255, 0, 0);

	/* 主容器设置 */
	lv_touch.box_eraser_cont = lv_obj_create(lv_scr_act());
	lv_obj_set_style_radius(lv_touch.box_eraser_cont, 0, LV_STATE_DEFAULT);         /* 设置背景颜色 */
    lv_obj_set_size(lv_touch.box_eraser_cont, 320, 480);                            /* 容器高度 */
    lv_obj_align(lv_touch.box_eraser_cont, LV_ALIGN_BOTTOM_MID, 0, 0);
	lv_obj_set_style_border_width(lv_touch.box_eraser_cont, 0, LV_STATE_DEFAULT);   /* 设置边框宽度为 0 */
    lv_obj_set_style_bg_color(lv_touch.box_eraser_cont, lv_color_hex(0x202020), 0);
    lv_obj_clear_flag(lv_touch.box_eraser_cont, LV_OBJ_FLAG_SCROLLABLE);

	/* 为画布申请缓冲区内存 */
	lv_touch.cbuf = heap_caps_malloc(width * height * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (lv_touch.cbuf == NULL)
    {
        ESP_LOGW("TAG", "cbuf malloc failed");
        if (lv_touch.box_eraser_cont != NULL && lv_obj_is_valid(lv_touch.box_eraser_cont))
        {
            lv_obj_del(lv_touch.box_eraser_cont);
        }
        lv_touch.box_eraser_cont = NULL;
        return;
    }

	/* 画布设置 */
	lv_touch.lv_touch_cont = lv_canvas_create(lv_touch.box_eraser_cont);
	lv_obj_set_size(lv_touch.lv_touch_cont, 320, 320);
	lv_obj_align(lv_touch.lv_touch_cont, LV_ALIGN_TOP_MID, 0, -15);
	lv_canvas_set_buffer(lv_touch.lv_touch_cont, lv_touch.cbuf, width, height, LV_IMG_CF_TRUE_COLOR);
	lv_canvas_fill_bg(lv_touch.lv_touch_cont, lv_color_hex(0xFFFFFF), LV_OPA_COVER);
	lv_obj_add_flag(lv_touch.lv_touch_cont, LV_OBJ_FLAG_CLICKABLE);

	/* 工具栏容器 */
	lv_touch.box_cont = lv_obj_create(lv_touch.box_eraser_cont);
    lv_obj_set_size(lv_touch.box_cont, 200, 50); 
    lv_obj_align_to(lv_touch.box_cont, lv_touch.lv_touch_cont, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(lv_touch.box_cont, lv_color_make(40,42,45), 0);
    lv_obj_set_style_radius(lv_touch.box_cont, 0, LV_STATE_DEFAULT);             /* 无圆角 */
    lv_obj_set_style_border_opa(lv_touch.box_cont, LV_OPA_0, LV_STATE_DEFAULT);  /* 边界透明 */

    /* 画笔按钮 */
	lv_touch.box_label_pen = lv_label_create(lv_touch.box_cont);
    lv_obj_set_style_text_font(lv_touch.box_label_pen, &lv_font_montserrat_16, 0); 
    lv_label_set_text(lv_touch.box_label_pen, LV_SYMBOL_EDIT);
    lv_obj_align(lv_touch.box_label_pen, LV_ALIGN_LEFT_MID, 15, 0);
    lv_obj_set_style_bg_color(lv_touch.box_label_pen,lv_color_hex(0xFFFFFF),LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(lv_touch.box_label_pen,LV_OPA_20,LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(lv_touch.box_label_pen,lv_color_hex(0xFFFFFF),LV_STATE_DEFAULT);
    lv_obj_add_flag(lv_touch.box_label_pen, LV_OBJ_FLAG_CLICKABLE);
    /* 添加事件 */
    lv_obj_add_event_cb(lv_touch.box_label_pen, touch_event_cb, LV_EVENT_ALL, NULL);

	/* 橡皮擦按钮 */
	lv_touch.box_label_eraser = lv_label_create(lv_touch.box_cont);
    lv_label_set_text(lv_touch.box_label_eraser, LV_SYMBOL_DRIVE);
    lv_obj_align(lv_touch.box_label_eraser, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(lv_touch.box_label_eraser,lv_color_hex(0xFFFFFF),LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(lv_touch.box_label_eraser,lv_color_hex(0xFFFFFF),LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(lv_touch.box_label_eraser,LV_OPA_20,LV_STATE_FOCUSED);
    lv_obj_add_flag(lv_touch.box_label_eraser, LV_OBJ_FLAG_CLICKABLE); 
    /* 添加事件 */
    lv_obj_add_event_cb(lv_touch.box_label_eraser, touch_event_cb, LV_EVENT_ALL, NULL);

	/* 颜色按钮 */
	lv_touch.box_label_color = lv_label_create(lv_touch.box_cont);
    lv_label_set_text(lv_touch.box_label_color, LV_SYMBOL_REFRESH);
    lv_obj_align(lv_touch.box_label_color, LV_ALIGN_RIGHT_MID, -15, 0);
    lv_obj_set_style_bg_color(lv_touch.box_label_color,lv_color_hex(0xFFFFFF),LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(lv_touch.box_label_color,LV_OPA_20,LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(lv_touch.box_label_color,lv_color_hex(0xFFFFFF),LV_STATE_DEFAULT);
    lv_obj_add_flag(lv_touch.box_label_color, LV_OBJ_FLAG_CLICKABLE); 
    /* 添加事件 */
    lv_obj_add_event_cb(lv_touch.box_label_color, touch_event_cb, LV_EVENT_ALL, NULL);

	/* 颜色选择器 */
	lv_touch.box_colorwheel = lv_colorwheel_create(lv_touch.box_eraser_cont, true);
    lv_obj_set_size(lv_touch.box_colorwheel, 120, 120); 
    lv_obj_align_to(lv_touch.box_colorwheel, lv_touch.lv_touch_cont, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);    
    /* 添加事件 */
    lv_obj_add_event_cb(lv_touch.box_colorwheel, touch_event_cb, LV_EVENT_ALL, NULL);

	/* 画笔尺寸滑块 */
	lv_touch.box_slider = lv_slider_create(lv_touch.box_eraser_cont);
    lv_obj_set_size(lv_touch.box_slider, 200, 20);      
    lv_slider_set_range(lv_touch.box_slider, 1, 15);    
    lv_obj_align_to(lv_touch.box_slider, lv_touch.lv_touch_cont, LV_ALIGN_OUT_BOTTOM_MID, 0, 40);     
    lv_slider_set_value(lv_touch.box_slider, 3, LV_ANIM_ON); /* 设置当前值 */
    /* 添加事件 */
    lv_obj_add_event_cb(lv_touch.box_slider, touch_event_cb, LV_EVENT_ALL, NULL);

	/* 标签调整 */
	lv_touch.box_slider_label = lv_label_create(lv_touch.box_eraser_cont);
	lv_obj_set_style_text_font(lv_touch.box_slider_label, &lv_font_montserrat_20, 0);
	lv_obj_align_to(lv_touch.box_slider_label, lv_touch.box_slider, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
	lv_touch.box_sound_label = lv_label_create(lv_touch.box_eraser_cont);
	lv_obj_align_to(lv_touch.box_sound_label, lv_touch.box_slider,  LV_ALIGN_OUT_LEFT_MID, -10, 0);

    lv_obj_add_flag(lv_touch.box_colorwheel, LV_OBJ_FLAG_HIDDEN);    /* 清除对象标志 */
    lv_obj_add_flag(lv_touch.box_slider, LV_OBJ_FLAG_HIDDEN);        /* 清除对象标志 */
    lv_obj_add_flag(lv_touch.box_slider_label, LV_OBJ_FLAG_HIDDEN);  /* 清除对象标志 */
    lv_obj_add_flag(lv_touch.box_sound_label, LV_OBJ_FLAG_HIDDEN);   /* 清除对象标志 */

	/* 创建触摸屏定时器任务 */
	lv_touch.touch_timer = lv_timer_create(lv_touch_task, 20, NULL);

	/* 隐藏box */
	lv_hidden_box();

	lv_obj_move_foreground(back_btn);
	app_obj_general.del_parent = lv_touch.box_eraser_cont;
	app_obj_general.APP_Function = lv_brush_del;
	app_obj_general.app_state = NOT_DEL_STATE;
}

/**
 * @brief       画笔界面退出
 * @param       无
 * @retval      无
 */
void lv_brush_del(void)
{
    if (lv_touch.touch_timer != NULL)
    {
	    lv_timer_del(lv_touch.touch_timer);
        lv_touch.touch_timer = NULL;
    }

    /* 删除画笔父类 */
    if (lv_touch.box_eraser_cont != NULL && lv_obj_is_valid(lv_touch.box_eraser_cont))
    {
        lv_obj_del(lv_touch.box_eraser_cont);
    }
    lv_touch.box_eraser_cont = NULL;
    lv_touch.lv_touch_cont = NULL;

    if (lv_touch.cbuf != NULL)
    {
        heap_caps_free(lv_touch.cbuf);
    }
    lv_touch.cbuf = NULL;
    box_pen_state = 0;
    box_pen_color_state = 0;
    /* 显示主界面 */
    lv_display_box();
}
