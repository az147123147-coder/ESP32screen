/**
 ******************************************************************************
 * @file        app_usb.c
 * @version     V1.0
 * @brief       LVGL USB APP
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

#include "app_usb.h"
#include "esp_rtc.h"


/* 声明USB应用图片资源 */
LV_IMG_DECLARE(lv_app_usb)

usb_ui_t lv_usb_ui = {0};                                /* 定义USB应用UI结构体并初始化为0 */
extern lv_obj_t * back_btn;                              /* 声明外部返回按钮对象 */
static void usb_check_timer_cb(lv_timer_t* timer);       /* USB状态检测定时器回调函数声明 */

/**
 * @brief       USB应用界面初始化
 * @param       无
 * @retval      无
 */
void app_usb_ui_init(void)
{
	/* 先执行清理操作 */
    if (lv_usb_ui.main_ui)
	{
        lv_usb_del(); 
    }
	
    /* 创建主容器 */
    lv_usb_ui.main_ui = lv_obj_create(lv_scr_act());
	lv_obj_set_style_radius(lv_usb_ui.main_ui, 0, LV_STATE_DEFAULT);   /* 无圆角 */
    lv_obj_set_style_bg_color(lv_usb_ui.main_ui, lv_color_hex(0x000000), LV_STATE_DEFAULT);	  /* 黑色背景 */
    lv_obj_set_style_border_width(lv_usb_ui.main_ui, 0, LV_STATE_DEFAULT);  /* 设置边框宽度为 0 */
    lv_obj_set_size(lv_usb_ui.main_ui, 320, 480);  /* 设置容器尺寸 */
    lv_obj_align(lv_usb_ui.main_ui, LV_ALIGN_TOP_MID, 0, 0);  /* 顶部居中对齐 */
    lv_obj_clear_flag(lv_usb_ui.main_ui, LV_OBJ_FLAG_SCROLLABLE); /* 禁用滚动 */

    /* 创建USB图标 */
    lv_usb_ui.usb_img = lv_img_create(lv_usb_ui.main_ui);
    lv_img_set_src(lv_usb_ui.usb_img, &lv_app_usb);
    lv_obj_align(lv_usb_ui.usb_img, LV_ALIGN_CENTER, 0, 0);

	/* 创建状态标签 */
    lv_usb_ui.status_label = lv_label_create(lv_usb_ui.main_ui);
    lv_obj_set_style_text_font(lv_usb_ui.status_label, &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_align(lv_usb_ui.status_label, LV_ALIGN_CENTER, 0, 120);  /* 图标下方120像素 */
    lv_label_set_text(lv_usb_ui.status_label, "");
    lv_obj_add_flag(lv_usb_ui.status_label, LV_OBJ_FLAG_HIDDEN);    /* 初始隐藏 */
    
    /* 初始化样式：默认灰色 */
    lv_obj_set_style_img_recolor(lv_usb_ui.usb_img, lv_color_hex(0x808080), LV_STATE_DEFAULT);
    lv_obj_set_style_img_recolor_opa(lv_usb_ui.usb_img, LV_OPA_COVER, LV_STATE_DEFAULT);

    /* 创建状态检测定时器 */
    lv_usb_ui.check_timer = lv_timer_create(usb_check_timer_cb, 500, NULL);

    /* 界面管理配置 */
    lv_hidden_box();
    lv_obj_move_foreground(back_btn);
    app_obj_general.del_parent = lv_usb_ui.main_ui;
    app_obj_general.APP_Function = lv_usb_del;
    app_obj_general.app_state = NOT_DEL_STATE;
}

/**
 * @brief       USB状态检测定时器回调函数
 * @param       timer: 定时器对象指针
 * @retval      无
 */
static void usb_check_timer_cb(lv_timer_t* timer)
{
	/* 有效性检查 */
    if (!lv_usb_ui.main_ui || !lv_usb_ui.usb_img) return;

    /* 检测USB连接状态 */
    if ((g_usbdev.status & 0x0f) == 0x01)
	{ 
        lv_obj_set_style_img_recolor(lv_usb_ui.usb_img, lv_color_hex(0x00FF00), LV_STATE_DEFAULT);   /* 已连接 */

		lv_label_set_text(lv_usb_ui.status_label, "connect success");
        lv_obj_set_style_text_color(lv_usb_ui.status_label, lv_color_hex(0x00FF00), 0);
        lv_obj_clear_flag(lv_usb_ui.status_label, LV_OBJ_FLAG_HIDDEN);
    } 
	else if ((g_usbdev.status & 0x0f) == 0x00)
	{                                
        lv_obj_set_style_img_recolor(lv_usb_ui.usb_img, lv_color_hex(0x808080), LV_STATE_DEFAULT);   /* 未连接 */

		lv_label_set_text(lv_usb_ui.status_label, "connect fail");
        lv_obj_set_style_text_color(lv_usb_ui.status_label, lv_color_hex(0xFF0000), 0);
        lv_obj_clear_flag(lv_usb_ui.status_label, LV_OBJ_FLAG_HIDDEN);

		/* 添加文字动画效果 */
		lv_obj_fade_in(lv_usb_ui.status_label, 300, 0);
    }
	else 
	{
        lv_obj_add_flag(lv_usb_ui.status_label, LV_OBJ_FLAG_HIDDEN);
    }   
}

/**
 * @brief       USB界面删除函数
 * @param       无
 * @retval      无
 */
void lv_usb_del(void)
{
    /* 删除定时器 */
    if (lv_usb_ui.check_timer) 
	{
        lv_timer_pause(lv_usb_ui.check_timer);   /* 暂停定时器 */
        lv_timer_del(lv_usb_ui.check_timer);     /* 删除定时器 */
        lv_usb_ui.check_timer = NULL;            /* 指针置空 */
    }

    /* 删除主容器 */
    if (lv_usb_ui.main_ui) 
	{
        lv_obj_del(lv_usb_ui.main_ui);           /* 删除主容器 */
        lv_usb_ui.main_ui = NULL;                /* 指针置空 */
        lv_usb_ui.usb_img = NULL;                /* 图片指针置空 */
        lv_usb_ui.status_label = NULL;           /* 标签指针置空 */
    }
    
    /* 恢复主界面显示 */
    lv_display_box();
}

