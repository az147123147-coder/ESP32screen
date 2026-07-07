/**
 ******************************************************************************
 * @file        xiaoxiaole.h
 * @version     V1.0
 * @brief       消消乐游戏 APP
 ******************************************************************************
 */

#ifndef XIAOXIAOLE_H
#define XIAOXIAOLE_H

#include "lvgl.h"
#include <stdio.h>
#include <math.h>
#include "lv_main_ui.h"
#include "lvgl_demo.h"
#include "lcd.h"


/* 颜色枚举定义 */
typedef enum {
    red = 0xff0000,     /* 红色 */
    green = 0x00ff00,   /* 绿色 */
    blue = 0x0000ff,    /* 蓝色 */
    gblue = 0x00ffff,   /* 青色 */
    yellow = 0xffff00,  /* 黄色 */
    rblue = 0xff00ff,   /* 品红 */
    orange = 0xff9200,  /* 橙色 */
} color_type_enum;

/* 方向枚举定义 */
typedef enum {
    up,     /* 上 */
    down,   /* 下 */
    left,   /* 左 */
    right,  /* 右 */
} direction_type_enum;

/* 游戏对象结构体 */
typedef struct {
    lv_obj_t * obj;         /* LVGL对象指针 */
    uint8_t color_index;    /* 颜色索引 */
    uint8_t x;              /* X坐标（0-7） */
    uint8_t y;              /* Y坐标（0-7） */
    uint8_t alive;          /* 存活状态（1:存活 0:待消除） */
} game_obj_type;


/* 函数声明 */
void xiaoxiaole();
void lv_game_del(void);

#endif
