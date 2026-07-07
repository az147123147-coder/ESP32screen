/**
 ******************************************************************************
 * @file        lv_start_ui.c
 * @version     V1.0
 * @brief       LVGL 开机Ui
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

 #include "lv_start_ui.h"


 /* 声明WKS LOGO */
 LV_IMG_DECLARE(esp_logo)
 LV_IMG_DECLARE(esp_logo1)
 
 /* 声明开机控件结构体 */
 static lv_starting_obj_t my_start_obj;
 
 /**
  * @brief       设置缩放的回调函数
  * @param       var：结构体指针
  * @param       v：缩放比例（0-255，对应0%-100%）
  * @retval      无
  */
 static void set_scale(void * var, int32_t v)
 {
	 lv_starting_obj_t * start_obj = (lv_starting_obj_t *)var;
 
	 /* 缩放第一个图片 */
	 lv_coord_t target_w = start_obj->img_w * v / 255;
	 lv_coord_t target_h = start_obj->img_h * v / 255;
	 lv_obj_set_size(start_obj->logo_obj, target_w, target_h);
 
	 /* 缩放第二个图片 */
	 lv_coord_t target1_w = start_obj->img1_w * v / 255;
	 lv_coord_t target1_h = start_obj->img1_h * v / 255;
	 lv_obj_set_size(start_obj->logo1_obj, target1_w, target1_h);
 }
 
 /**
  * @brief       动画完成回调函数
  * @param       a：动画指针
  * @retval      无
  */
 static void anim_ready_cb(lv_anim_t *a)
 {
	 (void)a;
	 vTaskDelay(800);
	 lv_obj_del(my_start_obj.logo_box);     
	 lv_mian_ui();
 }
 
 /**
  * @brief       logo 开机界面
  * @param       无
  * @retval      无
  */
 void lv_start_ui(void)
 {
	 /* 创建全屏容器并设置Flex布局 */
	 my_start_obj.logo_box = lv_obj_create(lv_scr_act());
	 lv_obj_set_size(my_start_obj.logo_box, LV_PCT(100), LV_PCT(100));
	 lv_obj_center(my_start_obj.logo_box);
	 lv_obj_clear_flag(my_start_obj.logo_box, LV_OBJ_FLAG_SCROLLABLE);
	 
	 /* 设置垂直Flex布局，居中排列 */
	 lv_obj_set_flex_flow(my_start_obj.logo_box, LV_FLEX_FLOW_COLUMN);
	 lv_obj_set_flex_align(my_start_obj.logo_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	 lv_obj_set_style_pad_row(my_start_obj.logo_box, 10, 0); /* 设置行间距为10像素 */
 
	 /* 创建第一个Logo图片对象 */
	 my_start_obj.logo_obj = lv_img_create(my_start_obj.logo_box);
	 lv_img_set_src(my_start_obj.logo_obj, &esp_logo);
	 my_start_obj.img_w = esp_logo.header.w;  
	 my_start_obj.img_h = esp_logo.header.h;  
 
	 /* 创建第二个Logo图片对象 */
	 my_start_obj.logo1_obj = lv_img_create(my_start_obj.logo_box);
	 lv_img_set_src(my_start_obj.logo1_obj, &esp_logo1);
	 my_start_obj.img1_w = esp_logo1.header.w; 
	 my_start_obj.img1_h = esp_logo1.header.h; 
 
	 /* 设置初始尺寸（原始大小的20%）*/
	 lv_obj_set_size(my_start_obj.logo_obj, 
					my_start_obj.img_w * 20 / 100, 
					my_start_obj.img_h * 20 / 100);
	 lv_obj_set_size(my_start_obj.logo1_obj, 
					my_start_obj.img1_w * 20 / 100, 
					my_start_obj.img1_h * 20 / 100);
 
	 /* 配置缩放动画参数 */
	 lv_anim_t anim;
	 lv_anim_init(&anim);
	 lv_anim_set_var(&anim, &my_start_obj); 
	 lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)set_scale);
	 lv_anim_set_values(&anim, 50, 255);    /* 从20%缩放到100% */
	 lv_anim_set_time(&anim, 1000);
	 lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
	 lv_anim_set_ready_cb(&anim, anim_ready_cb);
	 lv_anim_start(&anim);
 }