/**
 ******************************************************************************
 * @file        exfuns.h
 * @version     V1.0
 * @brief       FATFS 扩展代码
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

#ifndef __EXFUNS_H_
#define __EXFUNS_H_

#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "ff.h"
#include "spi_sd.h"

extern FATFS *fs[FF_VOLUMES];
extern FIL *file;
extern FIL *ftemp;
extern UINT br, bw;
extern FILINFO fileinfo;
extern FF_DIR dir;
extern uint8_t *fatbuf;     /* SD卡数据缓存区 */


/* exfuns_file_type返回的类型定义
 * 根据表FILE_TYPE_TBL获得.在exfuns.c里面定义
 */
#define T_BIN   0x00    /* BIN文件 */
#define T_LRC   0x10    /* LRC文件 */
#define T_NES   0x20    /* NES文件 */
#define T_SMS   0x21    /* SMS文件 */
#define T_TEXT  0x30    /* TXT文件 */
#define T_C     0x31    /* C文件 */
#define T_H     0x32    /* H文件 */
#define T_WAV   0x40    /* WAV文件 */
#define T_MP3   0x41    /* MP3文件 */
#define T_APE   0x42    /* APE文件 */
#define T_FLAC  0x43    /* FLAC文件 */
#define T_BMP   0x51    /* BMP文件 */
#define T_JPG   0x52    /* JPG文件 */
#define T_JPEG  0x53    /* JPEG文件 */
#define T_GIF   0x54    /* GIF文件 */
#define T_PNG   0x55    /* GIF文件 */
#define T_AVI   0x60    /* AVI文件 */


/* 函数声明 */
uint8_t exfuns_init(void);                              /* 申请内存 */
uint8_t exfuns_file_type(char *fname);                  /* 识别文件类型 */
uint8_t exfuns_get_free(uint8_t *pdrv, uint32_t *total, uint32_t *free);    /* 得到磁盘总容量和剩余容量 */
uint32_t exfuns_get_folder_size(uint8_t *fdname);       /* 得到文件夹大小 */
uint8_t *exfuns_get_src_dname(uint8_t *dpfn);
uint8_t exfuns_file_copy(uint8_t(*fcpymsg)(uint8_t *pname, uint8_t pct, uint8_t mode), uint8_t *psrc, uint8_t *pdst, uint32_t totsize, uint32_t cpdsize, uint8_t fwmode);       /* 文件复制 */
uint8_t exfuns_folder_copy(uint8_t(*fcpymsg)(uint8_t *pname, uint8_t pct, uint8_t mode), uint8_t *psrc, uint8_t *pdst, uint32_t *totsize, uint32_t *cpdsize, uint8_t fwmode);   /* 文件夹复制 */

#endif
