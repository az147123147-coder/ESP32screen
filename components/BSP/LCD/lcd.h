/**
 ******************************************************************************
 * @file        lcd.h
 * @version     V1.0
 * @brief       LCD驱动代码
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

#ifndef __LCD_H__
#define __LCD_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xl9555.h"
#include <math.h>
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"

/* RGB_BL */
#define LCD_BL(x)       do { x ?                                 \
								xl9555_pin_write(BL_CTR_IO, 1):     \
								xl9555_pin_write(BL_CTR_IO, 0);     \
                        } while(0)

/* RGB_BL */
#define LCD_RST(x)       do { x ?                                 \
								xl9555_pin_write(LCD_RST_IO, 1):     \
								xl9555_pin_write(LCD_RST_IO, 0);     \
							} while(0)

/* 引脚定义 */
#define LCD_NUM_CS      GPIO_NUM_39
#define LCD_NUM_DC      GPIO_NUM_38
#define LCD_NUM_RD      GPIO_NUM_14
#define LCD_NUM_WR      GPIO_NUM_45
#define LCD_NUM_RST     GPIO_NUM_NC

#define GPIO_LCD_D0     GPIO_NUM_13
#define GPIO_LCD_D1     GPIO_NUM_12
#define GPIO_LCD_D2     GPIO_NUM_11
#define GPIO_LCD_D3     GPIO_NUM_10
#define GPIO_LCD_D4     GPIO_NUM_9
#define GPIO_LCD_D5     GPIO_NUM_46
#define GPIO_LCD_D6     GPIO_NUM_3
#define GPIO_LCD_D7     GPIO_NUM_8
#define GPIO_LCD_D8     GPIO_NUM_18
#define GPIO_LCD_D9     GPIO_NUM_17
#define GPIO_LCD_D10    GPIO_NUM_16
#define GPIO_LCD_D11    GPIO_NUM_15
#define GPIO_LCD_D12    GPIO_NUM_7
#define GPIO_LCD_D13    GPIO_NUM_6
#define GPIO_LCD_D14    GPIO_NUM_5
#define GPIO_LCD_D15    GPIO_NUM_4


/* 常用颜色值 */
#define WHITE           0xFFFF      /* 白色 */
#define BLACK           0x0000      /* 黑色 */
#define RED             0xF800      /* 红色 */
#define GREEN           0x07E0      /* 绿色 */
#define BLUE            0x001F      /* 蓝色 */ 
#define MAGENTA         0XF81F      /* 品红色/紫红色 = BLUE + RED */
#define YELLOW          0XFFE0      /* 黄色 = GREEN + RED */
#define CYAN            0X07FF      /* 青色 = GREEN + BLUE */  

/* 非常用颜色 */
#define BROWN           0XBC40      /* 棕色 */
#define BRRED           0XFC07      /* 棕红色 */
#define GRAY            0X8430      /* 灰色 */ 
#define DARKBLUE        0X01CF      /* 深蓝色 */
#define LIGHTBLUE       0X7D7C      /* 浅蓝色 */ 
#define GRAYBLUE        0X5458      /* 灰蓝色 */ 
#define LIGHTGREEN      0X841F      /* 浅绿色 */  
#define LGRAY           0XC618      /* 浅灰色(PANNEL),窗体背景色 */ 
#define LGRAYBLUE       0XA651      /* 浅灰蓝色(中间层颜色) */ 
#define LBBLUE          0X2B12      /* 浅棕蓝色(选择条目的反色) */ 

/* LCD信息结构体 */
typedef struct _lcd_obj_t
{
    uint16_t        width;          /* 宽度 */
    uint16_t        height;         /* 高度 */
    uint16_t        pwidth;         /* 宽度 */
    uint16_t        pheight;        /* 高度 */
    uint8_t         dir;            /* 横屏还是竖屏控制：0，竖屏；1，横屏。 */
    uint16_t        wramcmd;        /* 开始写gram指令 */
    uint16_t        setxcmd;        /* 设置x坐标指令 */
    uint16_t        setycmd;        /* 设置y坐标指令 */
    uint16_t        wr;             /* 命令/数据IO */
    uint16_t        cs;             /* 片选IO */
    uint16_t        dc;             /* dc */
    uint16_t        rd;             /* rd */
} lcd_obj_t;

/* lcd配置结构体 */
typedef struct _lcd_config_t
{
    void *user_ctx;                                             /* 回调函数传入参数 */
    esp_lcd_panel_io_color_trans_done_cb_t notify_flush_ready;  /* 刷新回调函数 */
}lcd_cfg_t;


/* 导出相关变量 */
extern lcd_obj_t lcd_dev;
extern esp_lcd_panel_handle_t panel_handle; /* LCD句柄 */
/* lcd相关函数 */
void lcd_init(lcd_cfg_t lcd_config);                                                                                    /* 初始化lcd */
void lcd_clear(uint16_t color);                                                                                         /* 清除屏幕 */
void lcd_display_dir(uint8_t dir);                                                                                      /* lcd显示方向设置 */
void lcd_draw_point(uint16_t x, uint16_t y, uint16_t color);                                                            /* lcd画点函数 */
void lcd_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t color);
void lcd_color_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t *color);                               /* 在指定区域内填充指定颜色块 */
void lcd_draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);                                 /* 画线 */
void lcd_draw_hline(uint16_t x, uint16_t y, uint16_t len, uint16_t color);
void lcd_draw_circle(uint16_t x0, uint16_t y0, uint8_t r, uint16_t color);                                              /* 画圆 */
void lcd_show_char(uint16_t x, uint16_t y, char chr, uint8_t size, uint8_t mode, uint16_t color);                       /* 在指定位置显示一个字符 */
void lcd_show_num(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint16_t color);                     /* 显示len个数字 */
void lcd_show_xnum(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint8_t mode, uint16_t color);      /* 扩展显示len个数字(高位是0也显示) */
void lcd_show_string(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t size, char *p, uint16_t color);   /* 显示字符串 */
void lcd_draw_rectangle(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,uint16_t color);                             /* 绘画矩形 */
void lcd_app_show_mono_icos(uint16_t x,uint16_t y,uint8_t width,uint8_t height,uint8_t *icosbase,uint16_t color,uint16_t bkcolor); /* 显示单色图标 */

#endif
