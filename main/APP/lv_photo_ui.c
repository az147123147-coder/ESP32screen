/**
 ******************************************************************************
 * @file        lv_photo_ui.c
 * @version     V1.0
 * @brief       LVGL 照片 APP
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

#include "lv_photo_ui.h"
#include <string.h>


LV_IMG_DECLARE(Vectorback_next)
LV_IMG_DECLARE(Vectorback_pro)
lv_photo_ui_t photo_cfig;
static lv_obj_t *photo_pre_hot = NULL;
static lv_obj_t *photo_next_hot = NULL;
extern lv_obj_t * back_btn;

extern lv_group_t *ctrl_g;
extern lv_indev_t *indev_keypad;    /* 按键组 */

/* PIC Task Configuration
 * Including: task handle, task priority, stack size, creation task
 */
#define PIC_PRIO      2                                 /* task priority */
#define PIC_STK_SIZE  5 * 1024                          /* task stack size */
#define PHOTO_CACHE_MAGIC 0x50353635U
#define PHOTO_CACHE_PATH_SIZE (255 * 2 + 8)
TaskHandle_t          PICTask_Handler;                  /* task handle */
extern TaskHandle_t VIDEOTask_Handler;
void pic(void *pvParameters);                           /* Task function */
void lv_pic_png_bmp_jpeg_decode(uint16_t w,uint16_t h,uint8_t * pic_buf);
static bool lv_photo_is_supported_type(uint8_t type);

typedef struct
{
    uint32_t magic;
    uint16_t target_w;
    uint16_t target_h;
    uint16_t img_w;
    uint16_t img_h;
    uint32_t data_size;
    uint32_t data_checksum;
    uint32_t src_size;
    uint16_t src_date;
    uint16_t src_time;
} photo_cache_header_t;

static bool photo_cache_save_enabled = false;
static char photo_cache_path[PHOTO_CACHE_PATH_SIZE];
static photo_cache_header_t photo_cache_header;

/**
 * @brief       Obtain the total number of target files in the path path
 * @param       path : path
 * @retval      Total number of valid files
 */
uint16_t pic_get_tnum(char *path)
{
    uint8_t res;
    uint16_t rval = 0;
    FF_DIR tdir;
    FILINFO *tfileinfo;
    tfileinfo = (FILINFO *)malloc(sizeof(FILINFO));
	/* 选中SD卡 */
	SD_CS(0);
    res = f_opendir(&tdir, (const TCHAR *)path);

    if (res == FR_OK)
    {
        if (tfileinfo)
        {
            while (1)
            {
                res = f_readdir(&tdir, tfileinfo);

                if (res != FR_OK || tfileinfo->fname[0] == 0)break;
                res = exfuns_file_type(tfileinfo->fname);

                if (lv_photo_is_supported_type(res))
                {
                    rval++;
                }
            }
        }

        f_closedir(&tdir);
    }

	/* 取消选中SD卡 */
	SD_CS(1);
    free(tfileinfo);
    return rval;
}

/**
 * @brief       转换
 * @param       fs:文件系统对象
 * @param       clst:转换
 * @retval      =0:扇区号，0:失败
 */
static LBA_t atk_clst2sect(FATFS *fs, DWORD clst)
{
    clst -= 2;  /* Cluster number is origin from 2 */

    if (clst >= fs->n_fatent - 2)
    {
        return 0;   /* Is it invalid cluster number? */
    }

    return fs->database + (LBA_t)fs->csize * clst;  /* Start sector number of the cluster */
}

/**
 * @brief       偏移
 * @param       dp:指向目录对象
 * @param       Offset:目录表的偏移量
 * @retval      FR_OK(0):成功，!=0:错误
 */
FRESULT atk_photo_dir_sdi(FF_DIR *dp, DWORD ofs)
{
    DWORD clst;
    FATFS *fs = dp->obj.fs;

    if (ofs >= (DWORD)((FF_FS_EXFAT && fs->fs_type == FS_EXFAT) ? 0x10000000 : 0x200000) || ofs % 32)
    {
        /* Check range of offset and alignment */
        return FR_INT_ERR;
    }

    dp->dptr = ofs;         /* Set current offset */
    clst = dp->obj.sclust;  /* Table start cluster (0:root) */

    if (clst == 0 && fs->fs_type >= FS_FAT32)
    {	/* Replace cluster# 0 with root cluster# */
        clst = (DWORD)fs->dirbase;

        if (FF_FS_EXFAT)
        {
            dp->obj.stat = 0;
        }
        /* exFAT: Root dir has an FAT chain */
    }

    if (clst == 0)
    {	/* Static table (root-directory on the FAT volume) */
        if (ofs / 32 >= fs->n_rootdir)
        {
            return FR_INT_ERR;  /* Is index out of range? */
        }

        dp->sect = fs->dirbase;

    }
    else
    {   /* Dynamic table (sub-directory or root-directory on the FAT32/exFAT volume) */
        dp->sect = atk_clst2sect(fs, clst);
    }

    dp->clust = clst;   /* Current cluster# */

    if (dp->sect == 0)
    {
        return FR_INT_ERR;
    }

    dp->sect += ofs / fs->ssize;             /* Sector# of the directory entry */
    dp->dir = fs->win + (ofs % fs->ssize);   /* Pointer to the entry in the win[] */

    return FR_OK;
}

lv_img_dsc_t img_pic_dsc = {
    .header.always_zero = 0,
    .header.cf = LV_IMG_CF_TRUE_COLOR,
    .data = NULL,
};

static uint8_t *photo_img_buf = NULL;
static volatile bool photo_decode_ok = false;
static volatile bool photo_switching = false;

static void photo_sd_lost(void)
{
    lv_smail_icon_clear_state(TF_STATE);
    photo_switching = false;
    photo_cfig.pic_start = 0;

    if (app_obj_general.del_parent != NULL && app_obj_general.APP_Function != NULL)
    {
        app_obj_general.app_state = DEL_STATE;
    }
}

static bool lv_photo_is_supported_type(uint8_t type)
{
    return type == T_BMP || type == T_JPG || type == T_JPEG || type == T_PNG;
}

static uint32_t lv_photo_cache_checksum(const uint8_t *buf, uint32_t len)
{
    uint32_t hash = 2166136261U;

    for (uint32_t i = 0; i < len; i++)
    {
        hash ^= buf[i];
        hash *= 16777619U;
    }

    return hash;
}

static bool lv_photo_make_cache_path(const char *src, char *dst, size_t dst_size)
{
    size_t src_len = strlen(src);
    const char *slash = strrchr(src, '/');
    const char *dot = strrchr(src, '.');
    const char *cache_ext = ".C56";

    switch (exfuns_file_type((char *)src))
    {
        case T_JPG:
        case T_JPEG:
            cache_ext = ".J56";
            break;
        case T_PNG:
            cache_ext = ".P56";
            break;
        default:
            cache_ext = ".C56";
            break;
    }

    if (src_len + 1 > dst_size)
    {
        return false;
    }

    strcpy(dst, src);

    if (dot != NULL && (slash == NULL || dot > slash))
    {
        size_t base_len = (size_t)(dot - src);

        if (base_len + strlen(cache_ext) + 1 > dst_size)
        {
            return false;
        }

        strcpy(dst + base_len, cache_ext);
    }
    else
    {
        if (src_len + strlen(cache_ext) + 1 > dst_size)
        {
            return false;
        }

        strcat(dst, cache_ext);
    }

    return true;
}

static bool lv_photo_get_src_info(const char *src, uint32_t *src_size, uint16_t *src_date, uint16_t *src_time)
{
    FILINFO info;
    FRESULT res;

    SD_CS(0);
    res = f_stat(src, &info);
    SD_CS(1);

    if (res != FR_OK)
    {
        return false;
    }

    *src_size = (uint32_t)info.fsize;
    *src_date = info.fdate;
    *src_time = info.ftime;
    return true;
}

static bool lv_photo_cache_header_valid(const photo_cache_header_t *header, uint16_t target_w, uint16_t target_h, uint32_t src_size, uint16_t src_date, uint16_t src_time)
{
    uint32_t data_size;
    uint32_t max_data_size;

    if (header->magic != PHOTO_CACHE_MAGIC ||
        header->target_w != target_w ||
        header->target_h != target_h ||
        header->img_w == 0 ||
        header->img_h == 0 ||
        header->img_w > target_w ||
        header->img_h > target_h ||
        header->src_size != src_size ||
        header->src_date != src_date ||
        header->src_time != src_time)
    {
        return false;
    }

    data_size = (uint32_t)header->img_w * header->img_h * 2;
    max_data_size = (uint32_t)target_w * target_h * 2;
    return header->data_size == data_size && header->data_size <= max_data_size;
}

static void lv_photo_cache_begin(const char *src, uint16_t target_w, uint16_t target_h)
{
    uint32_t src_size;
    uint16_t src_date;
    uint16_t src_time;

    photo_cache_save_enabled = false;

    if (!lv_photo_make_cache_path(src, photo_cache_path, sizeof(photo_cache_path)))
    {
        return;
    }

    if (!lv_photo_get_src_info(src, &src_size, &src_date, &src_time))
    {
        return;
    }

    photo_cache_header.magic = PHOTO_CACHE_MAGIC;
    photo_cache_header.target_w = target_w;
    photo_cache_header.target_h = target_h;
    photo_cache_header.img_w = 0;
    photo_cache_header.img_h = 0;
    photo_cache_header.data_size = 0;
    photo_cache_header.data_checksum = 0;
    photo_cache_header.src_size = src_size;
    photo_cache_header.src_date = src_date;
    photo_cache_header.src_time = src_time;
    photo_cache_save_enabled = true;
}

static void lv_photo_cache_end(void)
{
    photo_cache_save_enabled = false;
}

static void lv_photo_cache_save(uint16_t w, uint16_t h, const uint8_t *buf)
{
    FIL *fp;
    FRESULT res;
    UINT bw = 0;
    photo_cache_header_t header;
    photo_cache_header_t invalid_header;

    if (!photo_cache_save_enabled || buf == NULL || w == 0 || h == 0)
    {
        return;
    }

    header = photo_cache_header;
    header.img_w = w;
    header.img_h = h;
    header.data_size = (uint32_t)w * h * 2;
    header.data_checksum = lv_photo_cache_checksum(buf, header.data_size);
    invalid_header = header;
    invalid_header.magic = 0;

    fp = (FIL *)malloc(sizeof(FIL));
    if (fp == NULL)
    {
        return;
    }

    SD_CS(0);
    res = f_open(fp, photo_cache_path, FA_WRITE | FA_CREATE_ALWAYS);

    if (res == FR_OK)
    {
        res = f_write(fp, &invalid_header, sizeof(invalid_header), &bw);

        if (res == FR_OK && bw == sizeof(invalid_header))
        {
            bw = 0;
            res = f_write(fp, buf, header.data_size, &bw);
        }

        if (res == FR_OK && bw == header.data_size)
        {
            res = f_sync(fp);
        }

        if (res == FR_OK)
        {
            res = f_lseek(fp, 0);

            if (res == FR_OK)
            {
                bw = 0;
                res = f_write(fp, &header, sizeof(header), &bw);
            }

            if (res == FR_OK && bw == sizeof(header))
            {
                res = f_sync(fp);
            }
        }

        f_close(fp);
    }

    SD_CS(1);
    free(fp);
}

static bool lv_photo_cache_load(const char *src, uint16_t target_w, uint16_t target_h)
{
    FIL *fp;
    FRESULT res;
    UINT br = 0;
    uint8_t *buf;
    uint32_t src_size;
    uint16_t src_date;
    uint16_t src_time;
    photo_cache_header_t header;
    bool opened = false;
    uint32_t expected_file_size;

    if (!lv_photo_make_cache_path(src, photo_cache_path, sizeof(photo_cache_path)))
    {
        return false;
    }

    if (!lv_photo_get_src_info(src, &src_size, &src_date, &src_time))
    {
        return false;
    }

    fp = (FIL *)malloc(sizeof(FIL));
    if (fp == NULL)
    {
        return false;
    }

    SD_CS(0);
    res = f_open(fp, photo_cache_path, FA_READ);

    if (res == FR_OK)
    {
        opened = true;
        res = f_read(fp, &header, sizeof(header), &br);
    }

    SD_CS(1);

    if (res != FR_OK || br != sizeof(header) || !lv_photo_cache_header_valid(&header, target_w, target_h, src_size, src_date, src_time))
    {
        if (opened)
        {
            SD_CS(0);
            f_close(fp);
            SD_CS(1);
        }
        free(fp);
        return false;
    }

    expected_file_size = (uint32_t)sizeof(header) + header.data_size;

    if ((uint32_t)f_size(fp) != expected_file_size)
    {
        SD_CS(0);
        f_close(fp);
        SD_CS(1);
        free(fp);
        return false;
    }

    buf = (uint8_t *)malloc(header.data_size);
    if (buf == NULL)
    {
        SD_CS(0);
        f_close(fp);
        SD_CS(1);
        free(fp);
        return false;
    }

    SD_CS(0);
    res = f_read(fp, buf, header.data_size, &br);
    f_close(fp);
    SD_CS(1);
    free(fp);

    if (res != FR_OK || br != header.data_size)
    {
        free(buf);
        return false;
    }

    if (lv_photo_cache_checksum(buf, header.data_size) != header.data_checksum)
    {
        free(buf);
        return false;
    }

    lv_pic_png_bmp_jpeg_decode(header.img_w, header.img_h, buf);
    return photo_decode_ok;
}

static void lv_photo_free_img_buf(void)
{
    if (photo_img_buf != NULL)
    {
        free(photo_img_buf);
        photo_img_buf = NULL;
    }

    img_pic_dsc.data = NULL;
    img_pic_dsc.data_size = 0;
}

static void lv_photo_show_black(void)
{
    if (photo_cfig.photo_obj_t.photo_img != NULL)
    {
        lv_obj_add_flag(photo_cfig.photo_obj_t.photo_img, LV_OBJ_FLAG_HIDDEN);
        lv_img_cache_invalidate_src(&img_pic_dsc);
        lv_photo_free_img_buf();
    }

    if (photo_cfig.photo_box != NULL)
    {
        lv_obj_invalidate(photo_cfig.photo_box);
    }

}

/**
 * @brief       PNG/BMPJPEG/JPG decoding
 * @param       filename : file name
 * @param       width    : image width
 * @param       height   : image height
 * @retval      None
 */
void lv_pic_png_bmp_jpeg_decode(uint16_t w,uint16_t h,uint8_t * pic_buf)
{
    uint32_t data_size = w * h * 2;

    if (pic_buf == NULL || data_size == 0)
    {
        free(pic_buf);
        photo_switching = false;
        return;
    }

    lv_photo_cache_save(w, h, pic_buf);

    if (!lvgl_mux_lock(200))
    {
        free(pic_buf);
        photo_switching = false;
        return;
    }

    lv_img_cache_invalidate_src(&img_pic_dsc);
    lv_photo_free_img_buf();
    photo_img_buf = pic_buf;

    img_pic_dsc.header.w = w;
    img_pic_dsc.header.h = h;
    img_pic_dsc.data_size = data_size;
    img_pic_dsc.data = (const uint8_t *)photo_img_buf;
    lv_img_set_src(photo_cfig.photo_obj_t.photo_img,&img_pic_dsc);
    lv_obj_clear_flag(photo_cfig.photo_obj_t.photo_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_center(photo_cfig.photo_obj_t.photo_img);
    photo_decode_ok = true;
    photo_switching = false;
    lvgl_mux_unlock();
}

static bool lv_photo_get_file_by_index(uint16_t index)
{
    FRESULT res;
    FF_DIR dir;
    uint16_t count = 0;
    bool found = false;

    SD_CS(0);
    res = f_opendir(&dir, "0:/PICTURE");

    if (res == FR_OK)
    {
        while (1)
        {
            res = f_readdir(&dir, photo_cfig.pic_picfileinfo);

            if (res != FR_OK)
            {
                photo_sd_lost();
                break;
            }

            if (photo_cfig.pic_picfileinfo->fname[0] == 0)
            {
                break;
            }

            if (!lv_photo_is_supported_type(exfuns_file_type(photo_cfig.pic_picfileinfo->fname)))
            {
                continue;
            }

            if (count == index)
            {
                strcpy((char *)photo_cfig.pic_pname, "0:/PICTURE/");
                strcat((char *)photo_cfig.pic_pname, (const char *)photo_cfig.pic_picfileinfo->fname);
                found = true;
                break;
            }

            count++;
        }

        f_closedir(&dir);
    }
    else
    {
        photo_sd_lost();
    }

    SD_CS(1);
    return found;
}

/**
 * @brief       pic task
 * @param       pvParameters : parameters (not used)
 * @retval      None
 */
void pic(void *pvParameters)
{
    pvParameters = pvParameters;
    uint8_t file_type = 0;
    int screen_w = 0;
    int screen_h = 0;

    while(1)
    {
        if (!photo_cfig.pic_start)
        {
            PICTask_Handler = NULL;
            vTaskDelete(NULL);
        }

		/* 选中SD卡 */
        photo_cfig.pic_totpicnum = pic_get_tnum("0:/PICTURE");

        if (photo_cfig.pic_totpicnum == 0)
        {
            photo_sd_lost();
            photo_switching = false;
            vTaskDelay(10);
            continue;
        }

        if (photo_cfig.pic_curindex >= photo_cfig.pic_totpicnum)
        {
            photo_cfig.pic_curindex = 0;
        }

        while (photo_cfig.pic_start)
        {
            if (photo_cfig.pic_curindex >= photo_cfig.pic_totpicnum)
            {
                photo_cfig.pic_curindex = 0;
            }

            if (!lv_photo_get_file_by_index(photo_cfig.pic_curindex))
            {
                photo_cfig.pic_totpicnum = pic_get_tnum("0:/PICTURE");
                photo_switching = false;
                photo_cfig.pic_curindex++;

                if (photo_cfig.pic_curindex >= photo_cfig.pic_totpicnum)
                {
                    photo_cfig.pic_curindex = 0;
                }

                vTaskDelay(10);
                continue;
            }

            file_type = exfuns_file_type(photo_cfig.pic_pname);
            screen_w = 0;
            screen_h = 0;

            if (lvgl_mux_lock(200))
            {
                screen_w = lv_obj_get_width(lv_scr_act());
                screen_h = lv_obj_get_height(lv_scr_act());
                lvgl_mux_unlock();
            }

            if (screen_w <= 0 || screen_h <= 20)
            {
                photo_switching = false;
                vTaskDelay(10);
                continue;
            }

            photo_decode_ok = false;

            switch (file_type)
            {
                case T_BMP:
                    bmp_decode(photo_cfig.pic_pname,screen_w,screen_h,(void *)lv_pic_png_bmp_jpeg_decode);    /* BMP decode */
                    break;
                case T_JPG:
                case T_JPEG:
                    if (!lv_photo_cache_load(photo_cfig.pic_pname, (uint16_t)screen_w, (uint16_t)(screen_h - 20)))
                    {
                        lv_photo_cache_begin(photo_cfig.pic_pname, (uint16_t)screen_w, (uint16_t)(screen_h - 20));
                        jpeg_decode(photo_cfig.pic_pname,screen_w,screen_h - 20,(void *)lv_pic_png_bmp_jpeg_decode);   /* JPG/JPEG decode */
                        lv_photo_cache_end();
                    }
                    break;
                case T_PNG:
                    if (!lv_photo_cache_load(photo_cfig.pic_pname, (uint16_t)screen_w, (uint16_t)(screen_h - 20)))
                    {
                        lv_photo_cache_begin(photo_cfig.pic_pname, (uint16_t)screen_w, (uint16_t)(screen_h - 20));
                        png_decode(photo_cfig.pic_pname,screen_w,screen_h - 20,(void *)lv_pic_png_bmp_jpeg_decode);    /* PNG decode */
                        lv_photo_cache_end();
                    }
                    break;
                default:
                    photo_cfig.pic_state = PIC_NEXT;                                                                 /* Non image format */
                    break;
            }

            if (lv_photo_is_supported_type(file_type))
            {
                if (!photo_decode_ok)
                {
                    photo_switching = false;
                    photo_cfig.pic_curindex++;

                    if (photo_cfig.pic_curindex >= photo_cfig.pic_totpicnum)
                    {
                        photo_cfig.pic_curindex = 0;
                    }

                    vTaskDelay(10);
                    continue;
                }
            }

            while (photo_cfig.pic_start)
            {
                while (lv_smail_icon_get_state(TF_STATE) == 0 && photo_cfig.pic_start == 0x01)
                {
                    app_obj_general.app_state = DEL_STATE;
                    vTaskDelay(10);
                }

                if (photo_cfig.pic_state == PIC_PREV)
                {
                    if (photo_cfig.pic_curindex)
                    {
                        photo_cfig.pic_curindex--;
                    }
                    else
                    {
                        photo_cfig.pic_curindex = photo_cfig.pic_totpicnum - 1;
                    }

                    photo_cfig.pic_state = PIC_NULL;
                    break;
                }
                else if (photo_cfig.pic_state == PIC_NEXT)
                {
                    photo_cfig.pic_curindex++;

                    if (photo_cfig.pic_curindex >= photo_cfig.pic_totpicnum)
                    {
                        photo_cfig.pic_curindex = 0;
                    }

                    photo_cfig.pic_state = PIC_NULL;
                    break;
                }
                vTaskDelay(10);
            }

        }
		/* 取消选中SD卡 */
    }
}

/**
 * @brief  相册播放事件回调
 * @param  *e ：事件相关参数的集合，它包含了该事件的所有数据
 * @return 无
 */
static void pic_play_event_cb(lv_event_t *e)
{
    lv_obj_t *target = lv_event_get_target(e);      /* 获取触发源 */
    lv_event_code_t code = lv_event_get_code(e);    /* 获取事件类型 */
    
    if (code != LV_EVENT_CLICKED)
    {
        return;
    }

    if (photo_switching || photo_cfig.pic_state != PIC_NULL)
    {
        return;
    }

    if (target == photo_cfig.photo_obj_t.photo_next || target == photo_next_hot)                   /* 下一张 */
    {
        photo_switching = true;
        photo_cfig.pic_state = PIC_NEXT;
        lv_photo_show_black();
    }
    else if (target == photo_cfig.photo_obj_t.photo_pre || target == photo_pre_hot)              /* 上一张 */
    {
        photo_switching = true;
        photo_cfig.pic_state = PIC_PREV;
        lv_photo_show_black();
    }
}

void lv_photo_ui(void)
{
    if (PICTask_Handler != NULL || VIDEOTask_Handler != NULL)
    {
        lv_msgbox("Media closing");
        return ;
    }

    if (lv_smail_icon_get_state(TF_STATE))
    {
        FRESULT res;

        photo_switching = false;

		/* 选中SD卡 */
	    SD_CS(0);
        photo_cfig.pic_curindex = 0;
        res = f_opendir(&photo_cfig.picdir, "0:/PICTURE");

        if (res == FR_OK)
        {
            f_closedir(&photo_cfig.picdir);
        }

        SD_CS(1);

        if (res != FR_OK)
        {
            lv_msgbox("PICTURE folder error");
            return ;
        }
        
        photo_cfig.pic_totpicnum = pic_get_tnum("0:/PICTURE");

        if (photo_cfig.pic_totpicnum == 0)
        {
            lv_msgbox("No pic files");
            return ;
        }

        photo_cfig.pic_picfileinfo = (FILINFO *)malloc(sizeof(FILINFO));
        photo_cfig.pic_pname = malloc(255 * 2 + 1);
        photo_cfig.pic_picoffsettbl = NULL;

        if (!photo_cfig.pic_picfileinfo || !photo_cfig.pic_pname)
        {
            free(photo_cfig.pic_picfileinfo);
            free(photo_cfig.pic_pname);
            free(photo_cfig.pic_picoffsettbl);
            photo_cfig.pic_picfileinfo = NULL;
            photo_cfig.pic_pname = NULL;
            photo_cfig.pic_picoffsettbl = NULL;
            lv_msgbox("memory allocation failed");
            return ;
        }

        /* 隐藏box */
        lv_hidden_box();

        photo_cfig.pic_start = 0x01;
        photo_cfig.photo_box = lv_obj_create(lv_scr_act());
        lv_obj_set_size(photo_cfig.photo_box,lv_obj_get_width(lv_scr_act()),lv_obj_get_height(lv_scr_act()));
        lv_obj_set_style_bg_color(photo_cfig.photo_box, lv_color_make(0,0,0), LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(photo_cfig.photo_box,LV_OPA_100,LV_STATE_DEFAULT);
        lv_obj_set_style_border_opa(photo_cfig.photo_box,LV_OPA_0,LV_STATE_DEFAULT);
        lv_obj_set_style_radius(photo_cfig.photo_box, 0, LV_STATE_DEFAULT);
        lv_obj_set_pos(photo_cfig.photo_box,0,0);
        lv_obj_clear_flag(photo_cfig.photo_box, LV_OBJ_FLAG_SCROLLABLE);

        app_obj_general.del_parent = photo_cfig.photo_box;
        app_obj_general.APP_Function = lv_pic_del;
        app_obj_general.app_state = NOT_DEL_STATE;

        photo_cfig.photo_obj_t.photo_img = lv_img_create(photo_cfig.photo_box);
        lv_obj_set_style_bg_color(photo_cfig.photo_obj_t.photo_img, lv_color_make(50,52,67), LV_STATE_DEFAULT);
        lv_obj_add_flag(photo_cfig.photo_obj_t.photo_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_center(photo_cfig.photo_obj_t.photo_img);
        lv_obj_move_background(photo_cfig.photo_obj_t.photo_img);
        photo_cfig.photo_obj_t.photo_number = NULL;

        int hot_w = lv_obj_get_width(lv_scr_act()) / 16;
        int hot_h = lv_obj_get_height(lv_scr_act()) / 8;

        if (hot_w < 24)
        {
            hot_w = 24;
        }

        if (hot_w > 36)
        {
            hot_w = 36;
        }

        if (hot_h < 36)
        {
            hot_h = 36;
        }

        if (hot_h > 56)
        {
            hot_h = 56;
        }

        photo_pre_hot = lv_obj_create(photo_cfig.photo_box);
        lv_obj_set_size(photo_pre_hot, hot_w, hot_h);
        lv_obj_align(photo_pre_hot, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_bg_opa(photo_pre_hot, LV_OPA_0, LV_STATE_DEFAULT);
        lv_obj_set_style_border_opa(photo_pre_hot, LV_OPA_0, LV_STATE_DEFAULT);
        lv_obj_set_style_radius(photo_pre_hot, 0, LV_STATE_DEFAULT);
        lv_obj_clear_flag(photo_pre_hot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(photo_pre_hot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(photo_pre_hot, pic_play_event_cb, LV_EVENT_ALL, NULL);

        photo_next_hot = lv_obj_create(photo_cfig.photo_box);
        lv_obj_set_size(photo_next_hot, hot_w, hot_h);
        lv_obj_align(photo_next_hot, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_bg_opa(photo_next_hot, LV_OPA_0, LV_STATE_DEFAULT);
        lv_obj_set_style_border_opa(photo_next_hot, LV_OPA_0, LV_STATE_DEFAULT);
        lv_obj_set_style_radius(photo_next_hot, 0, LV_STATE_DEFAULT);
        lv_obj_clear_flag(photo_next_hot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(photo_next_hot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(photo_next_hot, pic_play_event_cb, LV_EVENT_ALL, NULL);

        photo_cfig.photo_obj_t.photo_pre = lv_img_create(photo_cfig.photo_box);
        lv_img_set_src(photo_cfig.photo_obj_t.photo_pre,&Vectorback_pro);
        lv_obj_align(photo_cfig.photo_obj_t.photo_pre,LV_ALIGN_LEFT_MID,0,0);
        lv_obj_set_size(photo_cfig.photo_obj_t.photo_pre, Vectorback_pro.header.w, Vectorback_pro.header.h);
        lv_obj_add_flag(photo_cfig.photo_obj_t.photo_pre,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(photo_cfig.photo_obj_t.photo_pre, pic_play_event_cb, LV_EVENT_ALL, NULL);

        photo_cfig.photo_obj_t.photo_next = lv_img_create(photo_cfig.photo_box);
        lv_img_set_src(photo_cfig.photo_obj_t.photo_next,&Vectorback_next);
        lv_obj_align(photo_cfig.photo_obj_t.photo_next,LV_ALIGN_RIGHT_MID,0,0);
        lv_obj_set_size(photo_cfig.photo_obj_t.photo_next, Vectorback_next.header.w, Vectorback_next.header.h);
        lv_obj_add_flag(photo_cfig.photo_obj_t.photo_next,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(photo_cfig.photo_obj_t.photo_next, pic_play_event_cb, LV_EVENT_ALL, NULL);
        lv_obj_move_foreground(back_btn);

        if (indev_keypad != NULL)
        {
            lv_group_add_obj(ctrl_g, photo_cfig.photo_obj_t.photo_pre);
            lv_group_add_obj(ctrl_g, photo_cfig.photo_obj_t.photo_next);
            lv_group_focus_obj(photo_cfig.photo_obj_t.photo_pre);  /* 聚焦第一个APP */
        }

        if (PICTask_Handler == NULL)
        {
            xTaskCreatePinnedToCore((TaskFunction_t )pic,
                                    (const char*    )"pic",
                                    (uint16_t       )PIC_STK_SIZE,
                                    (void*          )NULL,
                                    (UBaseType_t    )PIC_PRIO,
                                    (TaskHandle_t*  )&PICTask_Handler,
                                    (BaseType_t     ) 1);
        }
		/* 取消选中SD卡 */
    }
    else
    {
        lv_msgbox("SD device not detected");
    }
}

/**
  * @brief  del pic
  * @param  None
  * @retval None
  */
void lv_pic_del(void)
{
    photo_cache_save_enabled = false;
    photo_decode_ok = false;
    photo_switching = false;
    photo_cfig.pic_start = 0;
    photo_cfig.pic_state = PIC_NULL;

    if (PICTask_Handler != NULL)
    {
        while (PICTask_Handler != NULL)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    photo_cache_save_enabled = false;
    SD_CS(1);

    lv_img_cache_invalidate_src(&img_pic_dsc);
    lv_photo_free_img_buf();

    if (photo_cfig.photo_box != NULL && lv_obj_is_valid(photo_cfig.photo_box))
    {
        lv_obj_del(photo_cfig.photo_box);
    }

    photo_cfig.photo_box = NULL;
    app_obj_general.del_parent = NULL;
    app_obj_general.APP_Function = NULL;
    app_obj_general.app_state = NOT_DEL_STATE;
    photo_cfig.photo_obj_t.photo_img = NULL;
    photo_cfig.photo_obj_t.photo_number = NULL;
    photo_cfig.photo_obj_t.photo_pre = NULL;
    photo_cfig.photo_obj_t.photo_next = NULL;
    photo_pre_hot = NULL;
    photo_next_hot = NULL;

    if (photo_cfig.pic_picfileinfo || photo_cfig.pic_pname || photo_cfig.pic_picoffsettbl)
    {
        free(photo_cfig.pic_picfileinfo);
        free(photo_cfig.pic_pname);
        free(photo_cfig.pic_picoffsettbl);
        photo_cfig.pic_picfileinfo = NULL;
        photo_cfig.pic_pname = NULL;
        photo_cfig.pic_picoffsettbl = NULL;
    }

    photo_cfig.pic_totpicnum = 0;
    photo_cfig.pic_curindex = 0;

    lv_display_box();
}
