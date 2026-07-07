/**
 ******************************************************************************
 * @file        image.h
 * @version     V1.0
 * @brief       图片库 代码(提供image_update_image和images_init用于图片库更新和初始化)
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

#ifndef __IMAGE_H
#define __IMAGE_H

#include "esp_partition.h"
#include "spi_flash_mmap.h"
#include "esp_log.h"
 #include "spi_sd.h"

/* 字体信息保存首地址
 * 占33个字节,第1个字节用于标记字库是否存在.后续每8个字节一组,分别保存起始地址和文件大小
 */
extern uint32_t IMAGEINFOADDR;

/* 字库信息结构体定义
 * 用来保存字库基本信息，地址，大小等
 */
typedef struct
{
    uint8_t imageok;             /* 图片库存在标志，0XBB，图片库正常；其他，图片库不存在 */
    
    uint32_t lvgl_camera_addr;
    uint32_t lvgl_camera_size;
  
    uint32_t lvgl_file_addr;
    uint32_t lvgl_file_size;
    
    uint32_t lvgl_video_addr;
    uint32_t lvgl_video_size;
    
    uint32_t lvgl_setting_addr;
    uint32_t lvgl_setting_size;
    
    uint32_t lvgl_weather_addr;
    uint32_t lvgl_weather_size;
    
    uint32_t lvgl_measure_addr;
    uint32_t lvgl_measure_size;

    uint32_t lvgl_photo_addr;
    uint32_t lvgl_photo_size;
    
    uint32_t lvgl_music_addr;
    uint32_t lvgl_music_size;

    uint32_t lvgl_calendar_addr;
    uint32_t lvgl_calendar_size;

    uint32_t lvgl_background_addr;
    uint32_t lvgl_background_size;
} _image_info;

/* 字库信息结构体 */
extern _image_info g_ftinfo;

/* 函数声明 */
uint8_t images_init(void);                                                                          /* 初始化图片 */
uint8_t images_update_image(uint16_t x, uint16_t y, uint8_t size, uint8_t *src, uint16_t color);    /* 更新图片文件 */
esp_err_t images_partition_write(void *buffer, uint32_t offset, uint32_t length);                   /* 分区表写入数据 */
esp_err_t images_partition_read(void *buffer, uint32_t offset, uint32_t length);                    /* 分区表读取数据 */
esp_err_t images_partition_erase_sector(uint32_t offset);                                           /* 擦除某个扇区 */

#endif
