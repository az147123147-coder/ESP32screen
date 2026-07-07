/**
 ******************************************************************************
 * @file        xiaoxiaole.c
 * @version     V1.0
 * @brief       消消乐游戏 APP
 ******************************************************************************
 */

 #include "lvgl.h"
 #include "stdlib.h"
 #include "xiaoxiaole.h"
 
 
 /* 静态全局变量声明 */
 static const unsigned int color_lib[7] = {red, green, blue, gblue, yellow, rblue, orange}; /* 颜色库 */
 static game_obj_type game_obj[8][8] = {0};          /* 8x8游戏对象数组 */
 static int score;                                    /* 游戏得分 */
 static lv_obj_t *screen1, *game_window, *refs_btn;  /* 屏幕和游戏窗口相关对象 */
 static lv_obj_t *bgmap, *next_btn, *pre_btn;        /* 背景地图和按钮 */
 static lv_obj_t *step_lable, *exit_btn, *coin;      /* 标签和退出按钮 */
 static lv_obj_t *score_lable;                       /* 分数标签 */
 static float screen_ratio;                          /* 屏幕比例系数 */
 static bool initialized = false;                    /* 游戏初始化标志位 */
 extern lv_obj_t * back_btn;
 
 
 /* 函数前置声明 */
 static void game_init(void);
 static void exchange_obj(game_obj_type *obj1, game_obj_type *obj2);
 static void move_obj_cb(lv_event_t *e);
 static void x_move_cb(void *var, int32_t v);
 static void y_move_cb(void *var, int32_t v);
 static void exchange_done_cb(lv_anim_t *a);
 static void same_color_move_to_coin(void);
 static int same_color_check(void);
 static void obj_move_down(void);
 static void set_obj_userdata(void);
 static void move_deleted_cb(lv_anim_t *a);
 static bool map_is_full(void);
 static bool has_same_color(void);
 static void map_del_all(void);
 static void map_refs(lv_event_t *e);
 static void exit_game_cb(lv_event_t *e);
 static void clear_all_clickable(void);
 static void add_all_clickable(void);
 static void move_to_coin_end_cb(lv_anim_t *a);
 static void flash_end_cb(lv_anim_t *a);
 static void flash_cb(void *var, int32_t v);
 static void same_color_flash(void);
 
 /* 图片资源声明 */
 LV_IMG_DECLARE(xiaoxiaole_bg_img)
 LV_IMG_DECLARE(refs_btn_img)
 LV_IMG_DECLARE(coin_img)
 
 /**
  * @brief   消消乐游戏主入口函数
  * @note    初始化游戏界面和核心组件
  */
 void xiaoxiaole()
 {
	 screen_ratio = 1;
 
	 /* 如果已经初始化过，直接显示界面 */
	 if(initialized) 
	 {
		 lv_obj_clear_flag(screen1, LV_OBJ_FLAG_HIDDEN);
		 lv_obj_move_foreground(back_btn);  // 关键点1：确保返回按钮置顶
		 lv_label_set_text_fmt(score_lable,"SCORE:%d",score);
		 
		 /* 关键点2：每次进入都刷新全局状态 */
		 app_obj_general.del_parent = screen1;
		 app_obj_general.APP_Function = lv_game_del; // 重新绑定回调
		 app_obj_general.app_state = NOT_DEL_STATE;
		 
		 return;
	 }
	 
	 /* 清除屏幕滚动标志 */
	 lv_obj_clear_flag(lv_scr_act(), LV_OBJ_FLAG_SCROLLABLE);
 
	 /* 创建主屏幕 */
	 screen1 = lv_tileview_create(lv_scr_act());
	 lv_obj_set_style_bg_color(screen1, lv_color_hex(0x000000), LV_PART_MAIN);
	 lv_obj_clear_flag(screen1, LV_OBJ_FLAG_SCROLLABLE);
 
	 /* 创建背景地图 */
	 bgmap = lv_img_create(screen1);
	 lv_img_set_src(bgmap, &xiaoxiaole_bg_img);
	 lv_img_set_pivot(bgmap, 0, 0);
	 //lv_img_set_zoom(bgmap, 256 * screen_ratio * 1.2);
	 lv_obj_clear_flag(bgmap, LV_OBJ_FLAG_SCROLLABLE);
 
	 /* 创建游戏主窗口 */
	 game_window = lv_tileview_create(screen1);
	 lv_obj_set_style_bg_color(game_window, lv_color_hex(0x000000), LV_PART_MAIN);
	 lv_obj_set_style_bg_opa(game_window, 200, LV_PART_MAIN);
	 lv_obj_clear_flag(game_window, LV_OBJ_FLAG_SCROLLABLE);
	 lv_obj_set_style_outline_width(game_window, 6, LV_PART_MAIN);
	 lv_obj_set_style_outline_color(game_window, lv_color_hex(0xbb7700), LV_PART_MAIN);
	 lv_obj_center(game_window);
	 lv_obj_set_size(game_window, 280 * screen_ratio, 280 * screen_ratio);
	 
	 /* 创建刷新按钮 */
	 refs_btn = lv_img_create(screen1);
	 lv_img_set_src(refs_btn, &refs_btn_img);
	 lv_obj_set_align(refs_btn, LV_ALIGN_TOP_RIGHT);
	 lv_obj_add_flag(refs_btn, LV_OBJ_FLAG_CLICKABLE);
	 lv_obj_add_event_cb(refs_btn, map_refs, LV_EVENT_CLICKED, 0);
 
	 /* 创建金币图标 */
	 coin = lv_img_create(screen1);
	 lv_img_set_src(coin, &coin_img);
	 
	 /* 初始化得分系统 */
	 score = 0;
	 score_lable = lv_label_create(screen1);
	 lv_label_set_text_fmt(score_lable,"SCORE:%d",score);
	 lv_obj_set_style_text_font(score_lable, &lv_font_montserrat_22, 0);
	 lv_obj_set_y(score_lable, 90);
	 lv_obj_align_to(score_lable, coin, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
	 lv_obj_set_style_text_color(score_lable, lv_color_hex(0x00aaff), LV_PART_MAIN);
	 
	 /* 初始化游戏对象 */
	 game_init();
 
	 /* 界面管理配置 */
	 lv_hidden_box();
	 lv_obj_move_foreground(back_btn);
	 app_obj_general.del_parent = screen1;
	 app_obj_general.APP_Function = lv_game_del;
	 app_obj_general.app_state = NOT_DEL_STATE;
 
	 initialized = true; // 标记已初始化
 }
 
 void lv_game_del(void)
 {
	 /* 隐藏游戏界面代替删除 */
	 if(lv_obj_is_valid(screen1)) 
	 {
		 lv_obj_add_flag(screen1, LV_OBJ_FLAG_HIDDEN);
	 }
	 
	 /* 必须重置APP_Function指向当前有效函数 */
	 app_obj_general.APP_Function = lv_game_del;
	 
	 /* 恢复主界面显示 */
	 lv_display_box();
 }
 
 /**
  * @brief   游戏初始化
  * @note    创建8x8游戏对象矩阵并初始化属性
  */
 static void game_init(void)
 {
	 int i, j;
	 score = 0; // 重置分数
	 lv_obj_refr_size(game_window);
 
	 /* 删除旧对象（如果存在） */
	 for(j = 0; j < 8; j++) 
	 {
		 for(i = 0; i < 8; i++) 
		 {
			 if(lv_obj_is_valid(game_obj[j][i].obj)) 
			 {
				 lv_obj_del(game_obj[j][i].obj);
			 }
		 }
	 }
	 
	 /* 初始化8x8游戏对象矩阵 */
	 for(j = 0; j < 8; j++) 
	 {
		 for(i = 0; i < 8; i++) 
		 {
			 /* 初始化对象属性 */
			 game_obj[j][i].x = i;
			 game_obj[j][i].y = j;
			 game_obj[j][i].alive = 1;
			 game_obj[j][i].color_index = rand() % 7;
			 
			 /* 创建按钮对象 */
			 game_obj[j][i].obj = lv_btn_create(game_window);
			 lv_obj_set_pos(game_obj[j][i].obj, i * 35 * screen_ratio + 1, j * 35 * screen_ratio + 1);
			 lv_obj_set_size(game_obj[j][i].obj, 35 * screen_ratio - 2, 35 * screen_ratio - 2);
			 lv_obj_set_style_bg_color(game_obj[j][i].obj, lv_color_hex(color_lib[game_obj[j][i].color_index]), 0);
			 /* 设置用户数据和事件回调 */
			 game_obj[j][i].obj->user_data = &game_obj[j][i];
			 lv_obj_add_event_cb(game_obj[j][i].obj, move_obj_cb, LV_EVENT_PRESSING, 0);
			 lv_obj_add_event_cb(game_obj[j][i].obj, move_obj_cb, LV_EVENT_RELEASED, 0);
		 }
	 }
 
	 /* 检查初始棋盘有效性 */
	 if(map_is_full() && same_color_check()) 
	 {
		 same_color_flash();
		 lv_obj_clear_flag(refs_btn, LV_OBJ_FLAG_CLICKABLE);
	 }    
 }
 
 /**
  * @brief       交换两个游戏对象
  * @param obj1  对象1指针
  * @param obj2  对象2指针
  */
 static void exchange_obj(game_obj_type *obj1, game_obj_type *obj2)
 {
	 game_obj_type temp;
	 
	 /* 禁用所有点击事件 */
	 clear_all_clickable();
	 
	 /* 保存临时颜色信息 */
	 temp.color_index = obj1->color_index;
	 temp.obj = obj1->obj;
 
	 /* 交换颜色索引 */
	 obj1->color_index = obj2->color_index;
	 obj2->color_index = temp.color_index;
			 
	 /* 创建X轴移动动画 */
	 lv_anim_t a1;
	 lv_anim_init(&a1);
	 lv_anim_set_var(&a1, obj1->obj);
	 lv_anim_set_exec_cb(&a1, x_move_cb);
	 lv_anim_set_time(&a1, 200);
	 if(!has_same_color()) { lv_anim_set_playback_time(&a1, 200); }
	 lv_anim_set_values(&a1, obj1->x * 35 * screen_ratio + 1, obj2->x * 35 * screen_ratio + 1);
	 lv_anim_set_path_cb(&a1, lv_anim_path_ease_out);
	 lv_anim_start(&a1);
		 
	 /* 创建Y轴移动动画 */
	 lv_anim_t a2;
	 lv_anim_init(&a2);
	 lv_anim_set_var(&a2, obj1->obj);
	 lv_anim_set_exec_cb(&a2, y_move_cb);
	 lv_anim_set_time(&a2, 200);
	 if(!has_same_color()) { lv_anim_set_playback_time(&a2, 200); }
	 lv_anim_set_values(&a2, obj1->y * 35 * screen_ratio + 1, obj2->y * 35 * screen_ratio + 1);
	 lv_anim_set_path_cb(&a2, lv_anim_path_ease_out);
	 lv_anim_start(&a2);
	 
	 /* 创建第二个对象X轴动画 */
	 lv_anim_t a3;
	 lv_anim_init(&a3);
	 lv_anim_set_var(&a3, obj2->obj);
	 lv_anim_set_exec_cb(&a3, x_move_cb);
	 lv_anim_set_time(&a3, 200);
	 if(!has_same_color()) { lv_anim_set_playback_time(&a3, 200); }
	 lv_anim_set_values(&a3, obj2->x * 35 * screen_ratio + 1, obj1->x * 35 * screen_ratio + 1);
	 lv_anim_set_path_cb(&a3, lv_anim_path_ease_out);
	 lv_anim_start(&a3);
 
	 /* 创建第二个对象Y轴动画（带完成回调） */
	 lv_anim_t a4;
	 lv_anim_init(&a4);
	 lv_anim_set_var(&a4, obj2->obj);
	 lv_anim_set_exec_cb(&a4, y_move_cb);
	 lv_anim_set_deleted_cb(&a4, exchange_done_cb);
	 lv_anim_set_time(&a4, 200);
	 if(!has_same_color()) { lv_anim_set_playback_time(&a4, 200); }
	 lv_anim_set_values(&a4, obj2->y * 35 * screen_ratio + 1, obj1->y * 35 * screen_ratio + 1);
	 lv_anim_set_path_cb(&a4, lv_anim_path_ease_out);
	 lv_anim_start(&a4);    
		 
	 /* 根据匹配结果处理对象交换 */
	 if(has_same_color()) 
	 {
		 /* 有效交换：交换对象指针 */
		 obj1->obj = obj2->obj;
		 obj2->obj = temp.obj;
		 obj1->obj->user_data = obj1;
		 obj2->obj->user_data = obj2;
	 } 
	 else 
	 {
		 /* 无效交换：恢复颜色 */
		 obj2->color_index = obj1->color_index;
		 obj1->color_index = temp.color_index;        
	 }    
 }
 
 /**
  * @brief       对象移动事件回调
  * @param e     事件指针
  */
 static void move_obj_cb(lv_event_t *e)
 {  
	 static lv_point_t click_point1, click_point2;
	 int movex, movey, direction;
	 static bool touched = false;
	 game_obj_type *stage_data = (game_obj_type *)(((lv_obj_t *)e->target)->user_data);
	 lv_obj_t *xxx = (lv_obj_t *)e->target;
 
	 if(touched == false) 
	 {    
		 /* 记录初始点击位置 */
		 touched = true;                 
		 lv_indev_get_point(lv_indev_get_act(), &click_point1);
		 return;
	 }
 
	 if(e->code == LV_EVENT_RELEASED) 
	 { 
		 touched = false;
		 lv_obj_move_foreground(xxx);
		 lv_indev_get_point(lv_indev_get_act(), &click_point2);
		 
		 /* 计算移动向量 */
		 movex = click_point2.x - click_point1.x;
		 movey = click_point2.y - click_point1.y;
		 
		 /* 过滤无效移动 */
		 if((movex == 0 && movey == 0) || (movex == movey) || (movex == -movey)) return;
		 
		 /* 确定移动方向 */
		 if((movex < 0 && movey < 0 && movex > movey) || 
			(movex > 0 && movey < 0 && movex < -movey)) direction = up;
		 if((movex > 0 && movey < 0 && movex > -movey) || 
			(movex > 0 && movey > 0 && movex > movey)) direction = right;
		 if((movex < 0 && movey < 0 && movex < movey) || 
			(movex < 0 && movey > 0 && movex < -movey)) direction = left;
		 if((movex < 0 && movey > 0 && movex > -movey) || 
			(movex > 0 && movey > 0 && movex < movey)) direction = down;
 
		 /* 处理边界情况并执行交换 */
		 switch(direction) 
		 {
			 case up:
				 if(stage_data->y != 0) 
					 exchange_obj(stage_data, &game_obj[stage_data->y-1][stage_data->x]);
				 break;
			 case down:
				 if(stage_data->y != 7) 
					 exchange_obj(stage_data, &game_obj[stage_data->y+1][stage_data->x]);
				 break;
			 case left:
				 if(stage_data->x != 0) 
					 exchange_obj(stage_data, &game_obj[stage_data->y][stage_data->x-1]);
				 break;
			 case right:
				 if(stage_data->x != 7) 
					 exchange_obj(stage_data, &game_obj[stage_data->y][stage_data->x+1]);
				 break;
		 }
	 }
 }
 
 /**
  * @brief       X轴移动动画回调
  * @param var   动画变量
  * @param v     目标值
  */
 static void x_move_cb(void *var, int32_t v)
 {
	 lv_obj_t *xxx = (lv_obj_t *)var;
	 lv_obj_set_x(xxx, v);    
 }
 
 /**
  * @brief       Y轴移动动画回调
  * @param var   动画变量
  * @param v     目标值
  */
 static void y_move_cb(void *var, int32_t v)
 {
	 lv_obj_t *xxx = (lv_obj_t *)var;
	 lv_obj_set_y(xxx, v);    
 }
 
 /**
  * @brief       闪烁动画回调
  * @param var   动画变量
  * @param v     透明度值
  */
 static void flash_cb(void *var, int32_t v)
 {
	 lv_obj_t *xxx = (lv_obj_t *)var;
	 lv_obj_set_style_bg_opa(xxx, 255 * (v % 2), 0);    
 }
 
 /**
  * @brief       交换完成回调
  * @param a     动画指针
  */
 static void exchange_done_cb(lv_anim_t *a)
 {    
	 if(same_color_check()) 
	 {
		 same_color_flash();
	 } 
	 else 
	 { 
		 add_all_clickable();
	 }            
 }
 
 /**
  * @brief   检查是否存在可消除颜色
  * @retval  true:存在 false:不存在
  */
 static bool has_same_color(void)
 {
	 /* 垂直方向检查 */
	 for(int i = 0; i < 8; i++) 
	 {
		 for(int j = 0; j < 6; j++) 
		 {
			 if(game_obj[j][i].color_index == game_obj[j+1][i].color_index && game_obj[j][i].color_index == game_obj[j+2][i].color_index) 
			 {
				 return true;
			 }    
		 }
	 }
	 
	 /* 水平方向检查 */
	 for(int j = 0; j < 8; j++) 
	 {
		 for(int i = 0; i < 6; i++) 
		 {
			 if(game_obj[j][i].color_index == game_obj[j][i+1].color_index && game_obj[j][i].color_index == game_obj[j][i+2].color_index) 
			 {
				 return true;
			 }            
		 }    
	 }    
	 return false;
 }
 
 /**
  * @brief       执行颜色消除检查
  * @retval      消除的匹配组数
  */
 static int same_color_check(void)
 {
	 int m = 0;
	 /* 垂直方向消除 */
	 for(int i = 0; i < 8; i++) 
	 {
		 for(int j = 0; j < 6; j++) 
		 {
			 if(game_obj[j][i].color_index == game_obj[j+1][i].color_index && game_obj[j][i].color_index == game_obj[j+2][i].color_index) 
			 {
				 game_obj[j][i].alive = 0;
				 game_obj[j+1][i].alive = 0;
				 game_obj[j+2][i].alive = 0;
				 m++;
			 }            
		 }    
	 }
	 
	 /* 水平方向消除 */
	 for(int j = 0; j < 8; j++) 
	 {
		 for(int i = 0; i < 6; i++) 
		 {
			 if(game_obj[j][i].color_index == game_obj[j][i+1].color_index && game_obj[j][i].color_index == game_obj[j][i+2].color_index) 
			 {
				 game_obj[j][i].alive = 0;
				 game_obj[j][i+1].alive = 0;
				 game_obj[j][i+2].alive = 0;
				 m++;
			 }            
		 }    
	 }    
	 return m;
 }
 
 /**
  * @brief   将被消除对象移动到金币位置
  */
 static void same_color_move_to_coin(void)
 {
	 for(int j = 0; j < 8; j++) 
	 {
		 for(int i = 0; i < 8; i++) 
		 {
			 if(game_obj[j][i].alive == 0) 
			 {
				 /* 移动对象到屏幕层 */
				 lv_obj_set_parent(game_obj[j][i].obj, screen1);
				 
				 /* 创建X轴移动动画 */
				 lv_anim_t a1;
				 lv_anim_init(&a1);
				 lv_anim_set_var(&a1, game_obj[j][i].obj);
				 lv_anim_set_exec_cb(&a1, x_move_cb);
				 lv_anim_set_time(&a1, 600);
				 lv_anim_set_path_cb(&a1, lv_anim_path_ease_in_out);
				 lv_anim_set_values(&a1, (i)*35*screen_ratio+1+lv_obj_get_x(game_window), 0);
				 lv_anim_start(&a1);    
				 
				 /* 创建Y轴移动动画（带完成回调） */
				 lv_anim_t a2;
				 lv_anim_init(&a2);
				 lv_anim_set_var(&a2, game_obj[j][i].obj);
				 lv_anim_set_exec_cb(&a2, y_move_cb);
				 lv_anim_set_time(&a2, 600);
				 lv_anim_set_path_cb(&a2, lv_anim_path_ease_in_out);
				 lv_anim_set_deleted_cb(&a2, move_to_coin_end_cb);
				 lv_anim_set_values(&a2, (j)*35*screen_ratio+1+lv_obj_get_y(game_window), 0);
				 lv_anim_start(&a2);    
			 }
		 }
	 }    
 }
 
 /**
  * @brief   执行闪烁效果
  */
 static void same_color_flash(void)
 {
	 for(int j = 0; j < 8; j++) 
	 {
		 for(int i = 0; i < 8; i++) 
		 {
			 if(game_obj[j][i].alive == 0) 
			 {
				 /* 创建闪烁动画 */
				 lv_anim_t a2;
				 lv_anim_init(&a2);
				 lv_anim_set_var(&a2, game_obj[j][i].obj);
				 lv_anim_set_exec_cb(&a2, flash_cb);
				 lv_anim_set_time(&a2, 600);
				 lv_anim_set_deleted_cb(&a2, flash_end_cb);
				 lv_anim_set_values(&a2, 1, 7);
				 lv_anim_start(&a2);    
			 }
		 }    
	 }        
 }
 
 /**
  * @brief   对象下落逻辑
  */
 static void obj_move_down(void)
 {
	 for(int i = 0; i < 8; i++) 
	 {
		 for(int j = 7; j > 0; j--) 
		 {
			 if(game_obj[j][i].alive == 0) 
			 {
				 /* 向下填充空位 */
				 for(int k = j; k > 0; k--) 
				 {
					 game_obj[k][i].alive = game_obj[k-1][i].alive;
					 game_obj[k][i].obj = game_obj[k-1][i].obj;
					 game_obj[k][i].color_index = game_obj[k-1][i].color_index;
					 
					 if(game_obj[k][i].alive) 
					 {
						 game_obj[k][i].obj->user_data = &game_obj[k][i];
						 /* 创建下落动画 */
						 lv_anim_t a1;
						 lv_anim_init(&a1);
						 lv_anim_set_var(&a1, game_obj[k][i].obj);
						 lv_anim_set_exec_cb(&a1, y_move_cb);
						 lv_anim_set_time(&a1, 150);
						 lv_anim_set_deleted_cb(&a1, move_deleted_cb);
						 lv_anim_set_values(&a1, (k-1)*35*screen_ratio+1, k*35*screen_ratio+1);
						 lv_anim_start(&a1);    
					 }                                    
				 }
				 
				 /* 生成新对象 */
				 game_obj[0][i].x = i;
				 game_obj[0][i].y = 0;
				 game_obj[0][i].alive = 1;
				 game_obj[0][i].color_index = rand()%7;
				 game_obj[0][i].obj = lv_btn_create(game_window);
				 lv_obj_set_pos(game_obj[0][i].obj, i*35*screen_ratio+1, -1*35*screen_ratio+1);
				 lv_obj_set_size(game_obj[0][i].obj, 35*screen_ratio-2, 35*screen_ratio-2);
				 lv_obj_set_style_bg_color(game_obj[0][i].obj, lv_color_hex(color_lib[game_obj[0][i].color_index]), 0);
				 game_obj[0][i].obj->user_data = &game_obj[0][i];
				 lv_obj_add_event_cb(game_obj[0][i].obj, move_obj_cb, LV_EVENT_PRESSING, 0);
				 lv_obj_add_event_cb(game_obj[0][i].obj, move_obj_cb, LV_EVENT_RELEASED, 0);
				 
				 /* 新对象下落动画 */
				 lv_anim_t a;
				 lv_anim_init(&a);
				 lv_anim_set_var(&a, game_obj[0][i].obj);
				 lv_anim_set_exec_cb(&a, y_move_cb);
				 lv_anim_set_time(&a, 150);
				 lv_anim_set_deleted_cb(&a, move_deleted_cb);
				 lv_anim_set_values(&a, (-1)*35*screen_ratio+1, 0*35*screen_ratio+1);
				 lv_anim_start(&a);    
				 break;
			 }
		 }
		 
		 /* 处理首行空位 */
		 if(game_obj[0][i].alive == 0) 
		 {
			 game_obj[0][i].x = i;
			 game_obj[0][i].y = 0;
			 game_obj[0][i].alive = 1;
			 game_obj[0][i].color_index = rand()%7;
			 game_obj[0][i].obj = lv_btn_create(game_window);
			 lv_obj_set_pos(game_obj[0][i].obj, i*35*screen_ratio+1, -1*35*screen_ratio+1);
			 lv_obj_set_size(game_obj[0][i].obj, 35*screen_ratio-2, 35*screen_ratio-2);
			 lv_obj_set_style_bg_color(game_obj[0][i].obj, lv_color_hex(color_lib[game_obj[0][i].color_index]), 0);
			 game_obj[0][i].obj->user_data = &game_obj[0][i];
			 lv_obj_add_event_cb(game_obj[0][i].obj, move_obj_cb, LV_EVENT_PRESSING, 0);
			 lv_obj_add_event_cb(game_obj[0][i].obj, move_obj_cb, LV_EVENT_RELEASED, 0);
			 
			 /* 新对象下落动画 */
			 lv_anim_t a2;
			 lv_anim_init(&a2);
			 lv_anim_set_var(&a2, game_obj[0][i].obj);
			 lv_anim_set_exec_cb(&a2, y_move_cb);
			 lv_anim_set_time(&a2, 150);
			 lv_anim_set_deleted_cb(&a2, move_deleted_cb);
			 lv_anim_set_values(&a2, (-1)*35*screen_ratio+1, 0*35*screen_ratio+1);
			 lv_anim_start(&a2);    
		 }                                            
	 }            
 }    
 
 /**
  * @brief       移动完成回调
  * @param a     动画指针
  */
 static void move_deleted_cb(lv_anim_t *a)
 {
	 if(lv_anim_count_running() == 0) 
	 {
		 obj_move_down();        
	 }
	 
	 if(map_is_full()) 
	 {
		 if(same_color_check()) 
		 {
			 same_color_flash();
			 set_obj_userdata();
		 } 
		 else 
		 {
			 lv_obj_add_flag(refs_btn, LV_OBJ_FLAG_CLICKABLE);
			 add_all_clickable();
		 }            
	 }
 }
 
 /**
  * @brief   更新对象用户数据
  */
 static void set_obj_userdata(void)
 {    
	 for(int j = 0; j < 8; j++) 
	 {
		 for(int i = 0; i < 8; i++) 
		 {
			 if(lv_obj_is_valid(game_obj[j][i].obj)) 
			 {
				 game_obj[j][i].obj->user_data = &game_obj[j][i];
			 }
		 }
	 }    
 }
 
 /**
  * @brief   检查棋盘是否填满
  * @retval  true:已满 false:未满
  */
 static bool map_is_full(void)
 {
	 for(int j = 0; j < 8; j++) 
	 {
		 for(int i = 0; i < 8; i++) 
		 {
			 if(game_obj[j][i].alive == 0) return false;
		 }
	 }    
	 return true;
 }    
 
 /**
  * @brief   删除所有对象
  */
 static void map_del_all(void)
 {
	 for(int j = 0; j < 8; j++) 
	 {
		 for(int i = 0; i < 8; i++) 
		 {
			 if(lv_obj_is_valid(game_obj[j][i].obj)) 
			 {
				 lv_obj_del(game_obj[j][i].obj);
			 }
		 }
	 }    
 }    
 
 /**
  * @brief       刷新地图事件回调
  * @param e     事件指针
  */
 static void map_refs(lv_event_t *e)
 {
	 lv_obj_clear_flag(refs_btn, LV_OBJ_FLAG_CLICKABLE);
	 map_del_all();
	 game_init();
 }    
 
 /**
  * @brief   禁用所有点击事件
  */
 static void clear_all_clickable(void)
 {
	 for(int j = 0; j < 8; j++) 
	 {
		 for(int i = 0; i < 8; i++) 
		 {
			 if(lv_obj_is_valid(game_obj[j][i].obj)) 
			 {
				 lv_obj_clear_flag(game_obj[j][i].obj, LV_OBJ_FLAG_CLICKABLE);
			 }
		 }
	 }                
 }
 
 /**
  * @brief   启用所有点击事件
  */
 static void add_all_clickable(void)
 {
	 for(int j = 0; j < 8; j++) 
	 {
		 for(int i = 0; i < 8; i++) 
		 {
			 if(lv_obj_is_valid(game_obj[j][i].obj)) 
			 {
				 lv_obj_add_flag(game_obj[j][i].obj, LV_OBJ_FLAG_CLICKABLE);
			 }
		 }
	 }                
 }
 
 /**
  * @brief       金币移动完成回调
  * @param a     动画指针
  */
 static void move_to_coin_end_cb(lv_anim_t *a)
 {
	 lv_obj_t *xxx = (lv_obj_t *)a->var;
	 lv_obj_del(xxx);
	 score += 10;
	 
	 if(lv_anim_count_running() == 0) 
	 {
		 lv_label_set_text_fmt(score_lable,"SCORE:%d",score);
		 obj_move_down();        
	 }
 }
 
 /**
  * @brief       闪烁完成回调
  * @param a     动画指针
  */
 static void flash_end_cb(lv_anim_t *a)
 {
	 if(lv_anim_count_running() == 0) 
	 {
		 same_color_move_to_coin();    
	 }
 }
 
 /**
  * @brief       退出游戏回调
  * @param e     事件指针
  */
 static void exit_game_cb(lv_event_t *e)
 {   
	 /* 只执行隐藏操作 */
	 lv_anim_del_all();
	 lv_game_del(); 
 }
 
