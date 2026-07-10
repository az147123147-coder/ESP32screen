/**
 ******************************************************************************
 * @file        app_calendar.c
 * @version     V1.0
 * @brief       LVGL 计算器 APP
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

#include "app_calculator.h"

calculator_ui_t calculator_ui;
extern lv_obj_t * back_btn;
/* 计算器参数 */
double lv_math_x1 = 0;           // 第一个操作数
double lv_math_x2 = 0;           // 第二个操作数
/* 计算结果 */
double lv_math_result = 0;       // 计算结果
/* 存储计算结果字符串 */
char str[100];
/* 计算器运算符类型 */
uint8_t lv_math_flag = 0;        // 当前操作符（+ - * /）
/* 正负数值互换 */
double cbp_and_negative = 0;
/* 逻辑标志位 */
uint8_t logical_flag_bit = 0x01;    // 标志位用于区分不同的状态

static void lv_math_reset(void)
{
    lv_math_x1 = 0;
    lv_math_x2 = 0;
    lv_math_result = 0;
    lv_math_flag = 0;
    cbp_and_negative = 0;
    logical_flag_bit = 0x01;
    memset(str, 0, sizeof(str));
}

/* 按钮矩阵的布局定义 */
static const char * btnm_map[] = {"AC", "+/-", "%", "/","\n",
                                  "7", "8", "9", "*","\n",
                                  "4", "5", "6", "-","\n",
                                  "1", "2", "3", "+","\n",
                                  "0", ".", "=",""
                                 };

/**
  * @brief  等于=按键计算
  * @param  x1:获取第一个数值
  * @param  x2:获取第二个数值
  * @param  ctype:计算的类型
  * @retval 返回计算数值
  */
double lv_math_calc(double x1, double x2, uint8_t ctype)
{
    switch(ctype)
    {
        case 0: /* 加法 */
          return x1 + x2;
        case 1: /* 减法 */
          return x1 - x2;
        case 2: /* 乘法 */
          return x1 * x2;
        case 3: /* 除法 */
          return x1 / x2;
        case 4: /* 没有任何运算符 */
          return x1 = fmod(x1, x2);  // 使用 fmod 函数计算余数;
        default:
          return 0; // 默认返回
    }
}

/**
  * @brief  将运算符标志转换为字符串
  * @param  lv_math_flag: 运算符标志（0:+, 1:-, 2:*, 3:/, 4:%）
  * @retval 对应的运算符字符串
  */
const char * lv_math_flag_to_operator(uint8_t lv_math_flag)
{
    switch(lv_math_flag)
    {
        case 4:  // %
            return "%";
        case 3:  // /
            return "/";
        case 2:  // *
            return "*";
        case 1:  // -
            return "-";
        case 0:  // +
            return "+";
        default:
            return "";  // 无效标志返回空字符串
    }
}

/**
  * @brief  按钮矩阵事件回调函数
  * @param  e: 事件对象
  * @retval 无
  */
 static void event_cb(lv_event_t * e)
 {
	 lv_event_code_t code = lv_event_get_code(e);
	 lv_obj_t * obj = lv_event_get_target(e);
 
	 /* 按钮绘制开始事件 */
	 if(code == LV_EVENT_DRAW_PART_BEGIN)
	 {
		 lv_obj_draw_part_dsc_t * dsc = lv_event_get_draw_part_dsc(e);
		 /* 确保是按钮矩阵的按钮绘制 */ 
		 if(dsc->class_p == &lv_btnmatrix_class && dsc->type == LV_BTNMATRIX_DRAW_PART_BTN)
		 {
			 bool pressed = false;
			 /* 检查当前按钮是否被按下 */ 
			 if (lv_btnmatrix_get_selected_btn(obj) == dsc->id && lv_obj_has_state(obj, LV_STATE_CHECKED))
			 {
				 pressed = true;
			 }
 
			 /* 设置默认按钮样式 */ 
			 dsc->label_dsc->ofs_x = 0;
			 dsc->label_dsc->color = lv_color_make(0, 0, 0); /* 黑色文字 */ 
			 dsc->rect_dsc->bg_color = lv_color_make(168, 168, 168); /* 灰色背景 */ 
			 dsc->rect_dsc->radius = 100; /* 圆形按钮 */ 
 
			 /* 按下状态样式 */ 
			 if (pressed)
			 {
				 dsc->rect_dsc->bg_opa = LV_OPA_50; /* 半透明 */ 
				 dsc->rect_dsc->bg_color = lv_palette_darken(LV_PALETTE_BLUE, 3); /* 深蓝色 */ 
			 }
 
			 dsc->rect_dsc->shadow_color = lv_color_make(0, 0, 0); /* 黑色阴影 */ 
 
			 /* 操作符按钮特殊样式 */
			 if (dsc->id == 3 || dsc->id == 7 || dsc->id == 11 || dsc->id == 15 || dsc->id == 18)
			 {
				 dsc->rect_dsc->bg_color = lv_color_make(253, 159, 11); /* 橙色 */ 
				 if (pressed)
				 {
					 dsc->rect_dsc->bg_opa = LV_OPA_50;
					 dsc->rect_dsc->bg_color = lv_palette_darken(LV_PALETTE_BLUE, 3);
				 }
				 dsc->label_dsc->color = lv_color_make(255, 255, 255); 
			 }
			 /* 数字按钮特殊样式 */ 
			 else if (dsc->id == 4 || dsc->id == 5 || dsc->id == 6
					 || dsc->id == 8 || dsc->id == 9 || dsc->id == 10
					 || dsc->id == 12 || dsc->id == 13 || dsc->id == 14
					 || dsc->id == 16 || dsc->id == 17)
			 {
				 dsc->rect_dsc->bg_color = lv_color_make(54, 54, 54); /* 深灰色 */ 
				 if (pressed)
				 {
					 dsc->rect_dsc->bg_opa = LV_OPA_50;
					 dsc->rect_dsc->bg_color = lv_palette_darken(LV_PALETTE_BLUE, 3);
				 }
				 dsc->label_dsc->color = lv_color_make(255, 255, 255); /* 白色文字 */ 
 
				 /* 数字"0"按钮特殊偏移（左对齐） */ 
				 if (dsc->id == 16 && dsc->draw_area != NULL)
				 {
					 dsc->label_dsc->ofs_x = -lv_area_get_width(dsc->draw_area) / 4;
				 }
			 }
		 }
	 }
	 /* 按钮值改变事件（按钮按下） */ 
	 else if (code == LV_EVENT_VALUE_CHANGED)
	 {
		 uint32_t id = lv_btnmatrix_get_selected_btn(obj);
		 const char * txt = lv_btnmatrix_get_btn_text(obj, id);
 
		 /* AC按钮处理（清除所有）*/ 
		 if (id == 0) 
		 {
			 lv_label_set_text(calculator_ui.calculator_dec, ""); /* 清除计算过程 */ 
			 lv_textarea_set_text(calculator_ui.calculator_text_area, "0"); /* 重置显示为0 */ 
			 lv_label_set_text(calculator_ui.calculator_result, "DEG"); /* 重置状态显示 */ 
			 lv_obj_set_style_text_color(calculator_ui.calculator_result, lv_color_make(0, 0, 0), LV_STATE_DEFAULT); 
			 lv_math_reset();
		 }
		 /* 等号按钮处理 */ 
		 else if (id == 18) 
		 {
			 /* 连续等号计算：使用上一次结果作为x1 */ 
			 if (logical_flag_bit == 0x02)  
			 {
				 lv_math_result = lv_math_calc(lv_math_result, lv_math_x2, lv_math_flag);
			 }
			 /* 首次等号计算 */ 
			 else 
			 {
				 lv_math_x2 = atof(lv_textarea_get_text(calculator_ui.calculator_text_area)); /* 获取当前显示值 */ 
				 lv_math_result = lv_math_calc(lv_math_x1, lv_math_x2, lv_math_flag); /* 执行计算 */
			 }
 
			 /* 格式化并显示结果 */ 
			 snprintf(str, sizeof(str), "%g", lv_math_result);
			 lv_textarea_set_text(calculator_ui.calculator_text_area, str);
			 lv_label_set_text(calculator_ui.calculator_result, "RES"); /* 显示结果标记 */ 
			 
			 /* 记录计算过程 */
			 snprintf(str, sizeof(str), "%.6g %s %.6g = %.6g", lv_math_x1, lv_math_flag_to_operator(lv_math_flag), lv_math_x2, lv_math_result);
			 lv_label_set_text(calculator_ui.calculator_dec, str);
 
			 lv_obj_set_style_text_color(calculator_ui.calculator_result, lv_color_make(255, 0, 0), LV_STATE_DEFAULT); 
			 lv_math_x1 = lv_math_result; /* 保存结果供连续计算 */ 
			 logical_flag_bit = 0x02;     /* 标记已按等号 */ 
		 }
		 /* 运算符按钮处理（包括%、/、*、-、+） */ 
		 else if (id == 2 || id == 3 || id == 7 || id == 11 || id == 15) 
		 {
			 /* 首次设置运算符时才获取第一个操作数 */ 
			 if (logical_flag_bit != 0x03) 
			 {
				 lv_math_x1 = atof(lv_textarea_get_text(calculator_ui.calculator_text_area));
			 }
 
			 /* 设置运算符标志 */ 
			 if (id == 2) lv_math_flag = 4;  // % 
			 else if (id == 3) lv_math_flag = 3;  // /
			 else if (id == 7) lv_math_flag = 2;  // *
			 else if (id == 11) lv_math_flag = 1; // -
			 else lv_math_flag = 0; // +
 
			 /* 更新运算符显示 */ 
			 lv_label_set_text(calculator_ui.calculator_result, txt);
			 lv_obj_set_style_text_color(calculator_ui.calculator_result, lv_color_make(0, 0, 0), LV_STATE_DEFAULT); 
			 
			 /* 重置输入框 */ 
			 lv_textarea_set_text(calculator_ui.calculator_text_area, "0");
			 logical_flag_bit = 0x03; /* 标记已选运算符 */ 
		 }
		 /* 正负号按钮处理 */ 
		 else if (id == 1) 
		 {
			 /* 获取当前值并取反 */ 
			 cbp_and_negative = atof(lv_textarea_get_text(calculator_ui.calculator_text_area));
			 cbp_and_negative = -cbp_and_negative;
			 snprintf(str, sizeof(str), "%g", cbp_and_negative);
			 lv_textarea_set_text(calculator_ui.calculator_text_area, str);
		 }
		 /* 数字和小数点按钮处理 */ 
		 else if ((id >= 4 && id <= 6) || (id >= 8 && id <= 10) || (id >= 12 && id <= 14) || id == 16 || id == 17)
		 {
			 /* 小数点按钮特殊处理 */ 
			 if (id == 17) 
			 {
				 /* 初始状态或运算符状态时初始化输入 */ 
				 if (logical_flag_bit & 0x07) /* 检查任何标志位 */ 
				 {
					 lv_textarea_set_text(calculator_ui.calculator_text_area, "0.");
					 logical_flag_bit = 0; /* 清除标志 */ 
				 } 
			 }
 
			 /* 状态变化时清空输入框 */ 
			 if (logical_flag_bit & 0x07) 
			 {
				 lv_textarea_set_text(calculator_ui.calculator_text_area, "");
				 logical_flag_bit = 0; /* 清除标志 */ 
			 }
 
			 /* 防止重复添加小数点 */ 
			 if (txt == btnm_map[21] && strstr(lv_textarea_get_text(calculator_ui.calculator_text_area), ".")) 
			 {
				 return;
			 }
 
			 /* 添加字符到输入框 */ 
			 if (txt[0] != '\0')
			 {
				 lv_textarea_add_char(calculator_ui.calculator_text_area, txt[0]);
			 }
		 }
	 }
 }

/**
  * @brief  初始化计算器应用
  * @param  无
  * @retval 无
  */
 void lv_app_calculator_init(void)
 {
	 lv_math_reset();
	/* 创建计算器主容器 */ 
	 calculator_ui.calculator_main_ui = lv_obj_create(lv_scr_act());
	 lv_obj_set_style_bg_color(calculator_ui.calculator_main_ui, lv_color_hex(0x000000), LV_STATE_DEFAULT); 
	 lv_obj_set_size(calculator_ui.calculator_main_ui, lv_obj_get_width(lv_scr_act()), lv_obj_get_height(lv_scr_act()));
	 lv_obj_set_style_radius(calculator_ui.calculator_main_ui, 0, LV_STATE_DEFAULT); 
	 lv_obj_set_style_border_opa(calculator_ui.calculator_main_ui, LV_OPA_0, LV_STATE_DEFAULT); 
	 lv_obj_set_pos(calculator_ui.calculator_main_ui, 0, 0);
	 lv_obj_clear_flag(calculator_ui.calculator_main_ui, LV_OBJ_FLAG_SCROLLABLE);  
	 lv_obj_update_layout(calculator_ui.calculator_main_ui);
 
	 /* 创建按钮矩阵 */ 
	 calculator_ui.calculator_btnm = lv_btnmatrix_create(calculator_ui.calculator_main_ui);
	 lv_obj_set_style_text_font(calculator_ui.calculator_btnm, &lv_font_montserrat_24, LV_STATE_DEFAULT); 
	 lv_obj_set_size(calculator_ui.calculator_btnm, lv_obj_get_width(lv_scr_act()) - 20, 280); 
	 lv_obj_set_style_bg_color(calculator_ui.calculator_btnm, lv_color_make(2, 2, 2), LV_STATE_DEFAULT);  
	 lv_obj_set_style_radius(calculator_ui.calculator_btnm, 0, LV_STATE_DEFAULT); 
	 lv_obj_set_style_border_opa(calculator_ui.calculator_btnm, LV_OPA_0, LV_STATE_DEFAULT); 
	 lv_btnmatrix_set_map(calculator_ui.calculator_btnm, btnm_map); 
	 lv_obj_align(calculator_ui.calculator_btnm, LV_ALIGN_BOTTOM_MID, 0, 10); 
	 lv_obj_add_event_cb(calculator_ui.calculator_btnm, event_cb, LV_EVENT_ALL, NULL); 
	 lv_btnmatrix_set_btn_width(calculator_ui.calculator_btnm, 16, 2); 
	 lv_obj_update_layout(calculator_ui.calculator_btnm);
 
	 /* 创建结果显示区域 */ 
	 calculator_ui.calculator_text_area = lv_textarea_create(calculator_ui.calculator_main_ui);
	 lv_textarea_set_text(calculator_ui.calculator_text_area, "0"); 
	 lv_textarea_set_one_line(calculator_ui.calculator_text_area, true); 
	 lv_textarea_set_max_length(calculator_ui.calculator_text_area, 32);
	 lv_textarea_set_cursor_click_pos(calculator_ui.calculator_text_area, false); 
	 lv_obj_set_size(calculator_ui.calculator_text_area, lv_obj_get_width(lv_scr_act()) - 30, 120); 
	 lv_obj_align_to(calculator_ui.calculator_text_area, calculator_ui.calculator_btnm, LV_ALIGN_OUT_TOP_MID, 0, -20); 
	 lv_obj_set_style_text_font(calculator_ui.calculator_text_area, &lv_font_montserrat_32, 0); 
	 lv_obj_set_style_text_align(calculator_ui.calculator_text_area, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN); 
	 lv_obj_clear_flag(calculator_ui.calculator_text_area, LV_OBJ_FLAG_CLICKABLE); 
 
	 /* 创建运算符状态标签 */ 
	 calculator_ui.calculator_result = lv_label_create(calculator_ui.calculator_main_ui);
	 lv_label_set_text(calculator_ui.calculator_result, "DEG");
	 lv_obj_set_style_text_font(calculator_ui.calculator_result, &lv_font_montserrat_24, 0); 
	 lv_obj_align_to(calculator_ui.calculator_result, calculator_ui.calculator_text_area, LV_ALIGN_BOTTOM_RIGHT, -10, 0); 
	 lv_obj_update_layout(calculator_ui.calculator_result);
 
	 /* 创建计算过程描述标签 */ 
	 calculator_ui.calculator_dec = lv_label_create(calculator_ui.calculator_main_ui);
	 lv_obj_set_width(calculator_ui.calculator_dec, lv_obj_get_width(calculator_ui.calculator_btnm)); 
	 lv_label_set_text(calculator_ui.calculator_dec, ""); 
	 lv_obj_set_style_text_font(calculator_ui.calculator_dec, &lv_font_montserrat_16, 0); 
	 lv_obj_set_style_text_opa(calculator_ui.calculator_dec, LV_OPA_50, LV_STATE_DEFAULT); 
	 lv_obj_align_to(calculator_ui.calculator_dec, calculator_ui.calculator_text_area, LV_ALIGN_OUT_TOP_MID, 0, -10);
	 lv_obj_set_style_text_align(calculator_ui.calculator_dec, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN); 
 
	 /* 隐藏其他界面元素 */ 
	 lv_hidden_box();
	 lv_obj_move_foreground(back_btn);
 
	 /* 设置全局应用对象参数 */ 
	 app_obj_general.del_parent = calculator_ui.calculator_main_ui; /* 设置删除对象 */ 
	 app_obj_general.APP_Function = lv_calculator_del; /* 设置退出函数 */ 
	 app_obj_general.app_state = NOT_DEL_STATE; /* 设置应用状态 */ 
 }

/**
 * @brief       计算器界面退出
 * @param       无
 * @retval      无
 */
void lv_calculator_del(void)
{
    /* 删除计算器父类 */
    lv_obj_del(calculator_ui.calculator_main_ui);
    /* 显示主界面 */
    lv_display_box();
}
