/**
 ******************************************************************************
 * @file        app_file.h
 * @version     V1.0
 * @brief       LVGL 文件 APP
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */
 
#ifndef __APP_FILE_H
#define __APP_FILE_H


#include "ff.h"
#include "lvgl.h"
#include "lv_main_ui.h"
#include "spi_sd.h"

#define FILE_PATH_SIZE 256
#define FILE_PATH_DEPTH 16

#define LIST_SIZE    100   /* 设置文件夹和文件的总量，自行定义 */
#define FILE_SEZE    1992  /* 设置读取文件内容的大小，自行定义 */

typedef struct
{
    lv_obj_t * list;          /* 列表控件 */
    lv_obj_t *lv_page_obj;    /* 定义标题的对象 */
    lv_obj_t * list_btn[LIST_SIZE]; /* 定义列表按键数量 */
    BYTE list_attr[LIST_SIZE];
    uint8_t list_flie_nuber;  /* 存储文本夹和文件的数量 */
    FRESULT fr;                 /* 读文件返回值 */
    FF_DIR lv_dir;              /* 读取的目录 */
    FILINFO SD_fno;             /* 文件信息结构体 */
    char *pname;              /* 带路径的文件名 */
    char *lv_pname;           /* 获取文件名 */
    char *lv_pname_shift;     /* 获取文件名中间存储 */
    const char* lv_pash;      /* 获取路径 */
    int lv_suffix_flag;       /* 检测后缀标志位 */
    int lv_prev_file_flag;    /* 上一个文件路径标志位 */
    char *lv_prev_file[LIST_SIZE];  /* 存储文件路径 */
    const void *image_scr;    /* 检测文件还是图片还是文档 */
    lv_obj_t * lv_back_obj;   /* 定义返回/菜单的对象 */
    lv_obj_t * lv_prev_btn;   /* 返回按键 */
    lv_obj_t * lv_back_btn;   /* 菜单按键 */
    lv_obj_t *lv_page_cont;   /* 显示文本的容器 */
    char rbuf[FILE_SEZE];     /* 获取文本的数据大小 */
    lv_obj_t *lv_return_page; /* 获取显示的页面 */
    lv_obj_t *lv_image_read;  /* 读取图片对象 */
}lv_file_struct;

/* 程序断言：用来调试代码的当term为1时，没有错误，当term为0时进入这个函数报错 */
#define FILE_ASSERT(term)                                                                                   \
do                                                                                                          \
{                                                                                                           \
    if (!(term))                                                                                            \
    {                                                                                                       \
        printf("Assert failed. Condition(%s). [%s][%d]\r\n", term, __FUNCTION__, __LINE__);                 \
        while(1)                                                                                            \
        {                                                                                                   \
            ;                                                                                               \
        }                                                                                                   \
    }                                                                                                       \
} while (0)

/* 函数声明 */
void app_file_init(void);    /* LVGL演示 */
void lv_file_del(void);

#endif
