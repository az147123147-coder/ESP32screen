/**
 ******************************************************************************
 * @file        lv_camera_ui.c
 * @version     V1.0
 * @brief       LVGL 相机 APP
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

#include "lv_camera_ui.h"


lv_usb_camera usb_camera;

#define DEMO_KEY_RESOLUTION "resolution"
static const char *TAG = "uvc_camera_lcd_demo";
#define DEMO_UVC_XFER_BUFFER_SIZE               ( 40 * 1024) /* 双缓冲 */
#define BIT0_FRAME_START                        (0x01 << 0)

typedef struct {
    uint16_t width;
    uint16_t height;
} camera_frame_size_t;

typedef struct {
    camera_frame_size_t camera_frame_size;
    uvc_frame_size_t *camera_frame_list;
    size_t camera_frame_list_num;
    size_t camera_currect_frame_index;
} camera_resolution_info_t;

static camera_resolution_info_t camera_resolution_info = {0};
static uint8_t *jpg_frame_buf1                         = NULL;
static uint8_t *jpg_frame_buf2                         = NULL;
static uint8_t *xfer_buffer_a                          = NULL;
static uint8_t *xfer_buffer_b                          = NULL;
static uint8_t *frame_buffer                           = NULL;
static PingPongBuffer_t *ppbuffer_handle               = NULL;
static uint16_t current_width                          = 0;
static uint16_t current_height                         = 0;
static volatile bool if_ppbuffer_init                  = false;
extern lv_obj_t * back_btn;

uint32_t pictureNumber = 0;
uint8_t res = 0;
UINT writelen = 0;
FIL *fftemp;
char file_name[30];

extern lv_group_t *ctrl_g;
extern lv_indev_t *indev_keypad;    /* 按键组 */

/**
 * @brief       Jpeg解码器一张图片
 * @param       input_buf       :输入数据
 * @param       len             :大小
 * @param       output_buf      :输出数据
 * @retval      无
 */
static int esp_jpeg_decoder_one_picture(uint8_t *input_buf, int len, uint8_t *output_buf)
{
    esp_err_t ret = ESP_OK;
    /* 生成默认配置 */
    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();

    /* jpeg解码器的空句柄 */
    jpeg_dec_handle_t jpeg_dec = NULL;
    jpeg_dec_io_t *jpeg_io = NULL;
    jpeg_dec_header_info_t *out_info = NULL;

    if ((usb_camera.usb_start & 0x02) != 0)
    {
        sprintf(file_name, "0:/PICTURE/img%ld.JPG", pictureNumber);
        fftemp = (FIL *)calloc(1,sizeof(FIL));

        if (fftemp == NULL)
        {
            ESP_LOGE(TAG, "img alloc err\r\n");
        }
        else
        {
            res = sd_f_open(fftemp, (const TCHAR *)file_name, FA_WRITE | FA_CREATE_NEW);
            if (res != FR_OK)
            {
                ESP_LOGE(TAG, "img open err\r\n");
            }
            else
            {
                res = sd_f_write(fftemp, (const void *)input_buf, len, &writelen);
                FRESULT close_res = sd_f_close(fftemp);
                if (res != FR_OK || writelen != len || close_res != FR_OK)
                {
                    ESP_LOGE(TAG, "img Write err");
                }
                else
                {
                    ESP_LOGI(TAG, "write buff len %d byte", writelen);
                    pictureNumber++;
                }
            }
            free(fftemp);
        }
        usb_camera.usb_start &= 0xFD;
    }
    
    /* 创建jpeg解码 */
    jpeg_dec = jpeg_dec_open(&config);
    if (jpeg_dec == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    /* 创建io回调句柄 */
    jpeg_io = calloc(1, sizeof(jpeg_dec_io_t));

    if (jpeg_io == NULL)
    {
        ret = ESP_ERR_NO_MEM;
        goto _exit;
    }

    /* 创建输出信息句柄 */
    out_info = calloc(1, sizeof(jpeg_dec_header_info_t));

    if (out_info == NULL)
    {
        ret = ESP_ERR_NO_MEM;
        goto _exit;
    }
    /* 设置输入缓冲区和缓冲区len为io_callback */
    jpeg_io->inbuf = input_buf;
    jpeg_io->inbuf_len = len;

    /* 解析jpeg图片头并获取用户和解码器的图片 */
    ret = jpeg_dec_parse_header(jpeg_dec, jpeg_io, out_info);

    if (ret < 0)
    {
        goto _exit;
    }

    jpeg_io->outbuf = output_buf;
    int inbuf_consumed = jpeg_io->inbuf_len - jpeg_io->inbuf_remain;
    jpeg_io->inbuf = input_buf + inbuf_consumed;
    jpeg_io->inbuf_len = jpeg_io->inbuf_remain;

    /* 开始解码jpeg原始数据 */
    ret = jpeg_dec_process(jpeg_dec, jpeg_io);

    if (ret < 0)
    {
        goto _exit;
    }

_exit:
    /* 反初始化解码器 */
    if (jpeg_dec != NULL)
    {
        jpeg_dec_close(jpeg_dec);
    }
    free(out_info);
    free(jpeg_io);
    return ret;
}

/**
  * @brief  拍照按键
  * @param  event : event
  * @retval None
  */
static void lv_phtbtn_control_event_handler(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t * obj = lv_event_get_target(event);

    // if (code == LV_EVENT_CLICKED || KEY1_PRES == LV_EVENT_CLICKED)
    // {
    //     if (usb_camera.usb_state != 0x00)
    //     {
    //         usb_camera.usb_start |= 0x02;
    //     }
        
    // }
}

/**
 * @brief       自适应JPG帧缓冲器
 * @param       length       :大小
 * @retval      无
 */
static esp_err_t adaptive_jpg_frame_buffer(size_t length)
{
    uint8_t *new_buf1 = (uint8_t *)heap_caps_aligned_alloc(16, length, MALLOC_CAP_SPIRAM);
    if (new_buf1 == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    uint8_t *new_buf2 = (uint8_t *)heap_caps_aligned_alloc(16, length, MALLOC_CAP_SPIRAM);
    if (new_buf2 == NULL)
    {
        free(new_buf1);
        return ESP_ERR_NO_MEM;
    }

    bool previous_init = if_ppbuffer_init;
    if_ppbuffer_init = false;
    esp_err_t ret = ppbuffer_create(ppbuffer_handle, new_buf2, new_buf1);
    if (ret != ESP_OK)
    {
        free(new_buf2);
        free(new_buf1);
        if_ppbuffer_init = previous_init;
        return ret;
    }

    free(jpg_frame_buf1);
    free(jpg_frame_buf2);
    jpg_frame_buf1 = new_buf1;
    jpg_frame_buf2 = new_buf2;
    if_ppbuffer_init = true;
    return ESP_OK;
}

/**
 * @brief       摄像头回调函数
 * @param       frame       :从UVC设备接收到的图像帧
 * @param       ptr         :转入参数（未使用）
 * @retval      无
 */
static void camera_frame_cb(uvc_frame_t *frame, void *ptr)
{
    (void)ptr;

    if (frame == NULL || frame->data == NULL || frame->width == 0 || frame->height == 0)
    {
        return;
    }

    if (!if_ppbuffer_init || current_width != frame->width || current_height != frame->height)
    {
        esp_err_t ret = adaptive_jpg_frame_buffer((size_t)frame->width * frame->height * 2);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "camera frame buffer allocation failed: %s", esp_err_to_name(ret));
            return;
        }
        current_width = frame->width;
        current_height = frame->height;
    }

    void *jpeg_buffer = NULL;
    /* 获取可写缓冲区 */
    if (ppbuffer_get_write_buf(ppbuffer_handle, &jpeg_buffer) != ESP_OK || jpeg_buffer == NULL)
    {
        return;
    }
    /* JPEG解码 */
    if (esp_jpeg_decoder_one_picture((uint8_t *)frame->data, frame->data_bytes, jpeg_buffer) != ESP_OK)
    {
        return;
    }
    /* 通知缓冲区写完成 */
    ppbuffer_set_write_done(ppbuffer_handle);
    vTaskDelay(pdMS_TO_TICKS(1));
}

lv_img_dsc_t img_dsc = {
    .header.always_zero = 0,
    .header.cf = LV_IMG_CF_TRUE_COLOR,
    .data = NULL,
};


/**
 * @brief       usb摄像头任务函数
 * @param       arg     :未使用
 * @retval      无
 */
static void usb_display_task(void *arg)
{
    uint16_t *lcd_buffer = NULL;
    int64_t count_start_time = 0;
    int frame_count = 0;
    int fps = 0;
    res = exfuns_init();
    pictureNumber = pic_get_tnum("0:/PICTURE");
    pictureNumber = pictureNumber + 1;

    while (1)
    {
        if (usb_camera.usb_state == 0x00 ||
            (usb_camera.usb_start & 0x01) == 0 ||
            !if_ppbuffer_init)
        {
            vTaskDelay(1);
            continue;
        }

        lcd_buffer = NULL;
        /* 获取可读缓冲区 */
        if (ppbuffer_get_read_buf(ppbuffer_handle, (void *)&lcd_buffer) == ESP_OK)
        {
            if (lcd_buffer != NULL && current_width == lcd_dev.width && current_height <= lcd_dev.height)
            {
                if (lvgl_mux_lock(100))
                {
                    if (usb_camera.usb_state != 0x00 &&
                        (usb_camera.usb_start & 0x01) != 0 &&
                        usb_camera.camera_buf.camera_header != NULL &&
                        lv_obj_is_valid(usb_camera.camera_buf.camera_header))
                    {
                        img_dsc.header.w = current_width;
                        img_dsc.header.h = current_height;
                        img_dsc.data_size = current_width * current_height * 2;
                        img_dsc.data = (const uint8_t *)lcd_buffer;
                        lv_img_set_src(usb_camera.camera_buf.camera_header, &img_dsc);
                        lv_refr_now(NULL);
                    }
                    lvgl_mux_unlock();
                }
            }
            /* 通知缓冲区读完成 */
            ppbuffer_set_read_done(ppbuffer_handle);

            if (count_start_time == 0)
            {
                count_start_time = esp_timer_get_time();
            }

            if (++frame_count == 20)
            {
                frame_count = 0;
                fps = 20 * 1000000 / (esp_timer_get_time() - count_start_time);
                count_start_time = esp_timer_get_time();
                ESP_LOGI(TAG, "camera fps: %d %d*%d", fps, current_width, current_height);
            }
        }

        vTaskDelay(1);
    }
}

/**
 * @brief       在nvs分区获取数值
 * @param       key     :名称
 * @param       value   :数据
 * @param       size    :大小
 * @retval      无
 */
static void usb_get_value_from_nvs(char *key, void *value, size_t *size)
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("memory", NVS_READWRITE, &my_handle);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    }
    else
    {
        err = nvs_get_blob(my_handle, key, value, size);
        switch (err)
        {
            case ESP_OK:
                break;
            case ESP_ERR_NVS_NOT_FOUND:
                ESP_LOGI(TAG, "%s is not initialized yet!", key);
                break;
            default :
                ESP_LOGE(TAG, "Error (%s) reading!\n", esp_err_to_name(err));
        }

        nvs_close(my_handle);
    }
}

/**
 * @brief       在nvs分区保存数值
 * @param       key     :名称
 * @param       value   :数据
 * @param       size    :大小
 * @retval      ESP_OK：设置成功；其他表示获取失败
 */
static esp_err_t usb_set_value_to_nvs(char *key, void *value, size_t size)
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("memory", NVS_READWRITE, &my_handle);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!\n", esp_err_to_name(err));
        return ESP_FAIL;
    }
    else
    {
        err = nvs_set_blob(my_handle, key, value, size);

        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "NVS set failed %s", esp_err_to_name(err));
        }

        err = nvs_commit(my_handle);

        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "NVS commit failed");
        }

        nvs_close(my_handle);
    }

    return err;
}

/**
 * @brief       USB数据流初始化
 * @param       无
 * @retval      ESP_OK：成功初始化；其他表示初始化失败
 */
static esp_err_t usb_stream_init(void)
{
    uvc_config_t uvc_config = {
        .frame_interval = FRAME_INTERVAL_FPS_30,
        .xfer_buffer_size = DEMO_UVC_XFER_BUFFER_SIZE,
        .xfer_buffer_a = xfer_buffer_a,
        .xfer_buffer_b = xfer_buffer_b,
        .frame_buffer_size = DEMO_UVC_XFER_BUFFER_SIZE,
        .frame_buffer = frame_buffer,
        .frame_cb = &camera_frame_cb,
        .frame_cb_arg = NULL,
        .frame_width = FRAME_RESOLUTION_ANY,
        .frame_height = FRAME_RESOLUTION_ANY,
        .flags = FLAG_UVC_SUSPEND_AFTER_START,
    };

    esp_err_t ret = uvc_streaming_config(&uvc_config);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "uvc streaming config failed");
    }
    return ret;
}

/**
 * @brief       查找USB摄像头当前的分辨率
 * @param       camera_frame_size :结构体
 * @retval      返回分辨率
 */
static size_t usb_camera_find_current_resolution(camera_frame_size_t *camera_frame_size)
{
    if (camera_resolution_info.camera_frame_list == NULL)
    {
        return -1;
    }

    size_t i = 0;
    while (i < camera_resolution_info.camera_frame_list_num)
    {
        if (camera_frame_size->width >= camera_resolution_info.camera_frame_list[i].width && camera_frame_size->height >= camera_resolution_info.camera_frame_list[i].height)
        {
            /* 查找下一个分辨率
               如果当前的分辨率最小，则切换到大的分辨率*/
            camera_frame_size->width = camera_resolution_info.camera_frame_list[i].width;
            camera_frame_size->height = camera_resolution_info.camera_frame_list[i].height;
            break;
        }
        else if (i == camera_resolution_info.camera_frame_list_num - 1)
        {
            camera_frame_size->width = camera_resolution_info.camera_frame_list[i].width;
            camera_frame_size->height = camera_resolution_info.camera_frame_list[i].height;
            break;
        }
        i++;
    }
    /* 打印当前分辨率 */
    ESP_LOGI(TAG, "Current resolution is %dx%d", camera_frame_size->width, camera_frame_size->height);
    return i;
}

/**
 * @brief       usb数据流回调函数
 * @param       event   : 事件
 * @param       arg     : 参数（未使用）
 * @retval      无
 */
static void usb_stream_state_changed_cd(usb_stream_state_t event,void *arg)
{
    switch(event)
    {
        /* 连接状态 */
        case STREAM_CONNECTED:
            usb_camera.usb_state = 0x00;
            lv_smail_icon_add_state(USB_STATE);
            
            /* 获取相机分辨率，并存储至nvs分区 */
            size_t size = sizeof(camera_frame_size_t);
            usb_get_value_from_nvs(DEMO_KEY_RESOLUTION, &camera_resolution_info.camera_frame_size, &size);
            size_t frame_index = 0;
            size_t frame_list_num = 0;
            esp_err_t list_ret = uvc_frame_size_list_get(NULL, &frame_list_num, NULL);

            if (list_ret == ESP_OK && frame_list_num > 0)
            {
                ESP_LOGI(TAG, "UVC: get frame list size = %u, current = %u", frame_list_num, frame_index);
                uvc_frame_size_t *_frame_list = (uvc_frame_size_t *)malloc(frame_list_num * sizeof(uvc_frame_size_t));
                if (_frame_list == NULL)
                {
                    ESP_LOGE(TAG, "camera source frame list allocation failed");
                    break;
                }

                list_ret = uvc_frame_size_list_get(_frame_list, NULL, NULL);
                if (list_ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "camera source frame list read failed: %s", esp_err_to_name(list_ret));
                    free(_frame_list);
                    break;
                }

                uvc_frame_size_t *resized_list = (uvc_frame_size_t *)realloc(
                    camera_resolution_info.camera_frame_list,
                    frame_list_num * sizeof(uvc_frame_size_t));
                if (resized_list == NULL)
                {
                    ESP_LOGE(TAG, "camera filtered frame list allocation failed");
                    free(_frame_list);
                    break;
                }
                camera_resolution_info.camera_frame_list = resized_list;

                for (size_t i = 0; i < frame_list_num; i++)
                {
                    if (_frame_list[i].width <= lcd_dev.width && _frame_list[i].height <= lcd_dev.height)
                    {
                        camera_resolution_info.camera_frame_list[frame_index++] = _frame_list[i];
                        ESP_LOGI(TAG, "\tpick frame[%u] = %ux%u", i, _frame_list[i].width, _frame_list[i].height);
                    }
                    else
                    {
                        ESP_LOGI(TAG, "\tdrop frame[%u] = %ux%u", i, _frame_list[i].width, _frame_list[i].height);
                    }
                }
                free(_frame_list);
                camera_resolution_info.camera_frame_list_num = frame_index;

                if (camera_resolution_info.camera_frame_list_num == 0)
                {
                    ESP_LOGE(TAG, "camera has no supported display resolution");
                    break;
                }

                if(camera_resolution_info.camera_frame_size.width != 0 && camera_resolution_info.camera_frame_size.height != 0) {
                    camera_resolution_info.camera_currect_frame_index = usb_camera_find_current_resolution(&camera_resolution_info.camera_frame_size);
                }
                else
                {
                    camera_resolution_info.camera_currect_frame_index = 0;
                }

                if (camera_resolution_info.camera_currect_frame_index >= camera_resolution_info.camera_frame_list_num)
                {
                    ESP_LOGE(TAG, "fine current resolution fail");
                    break;
                }
                esp_err_t reset_ret = uvc_frame_size_reset(camera_resolution_info.camera_frame_list[camera_resolution_info.camera_currect_frame_index].width,
                                                           camera_resolution_info.camera_frame_list[camera_resolution_info.camera_currect_frame_index].height,
                                                           FPS2INTERVAL(30));
                if (reset_ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "camera resolution reset failed: %s", esp_err_to_name(reset_ret));
                    break;
                }
                camera_frame_size_t camera_frame_size = {
                    .width = camera_resolution_info.camera_frame_list[camera_resolution_info.camera_currect_frame_index].width,
                    .height = camera_resolution_info.camera_frame_list[camera_resolution_info.camera_currect_frame_index].height,
                };

                esp_err_t nvs_ret = usb_set_value_to_nvs(DEMO_KEY_RESOLUTION, &camera_frame_size, sizeof(camera_frame_size_t));
                if (nvs_ret != ESP_OK)
                {
                    ESP_LOGW(TAG, "camera resolution save failed: %s", esp_err_to_name(nvs_ret));
                }
                /* 等待USB摄像头连接 */
                esp_err_t resume_ret = usb_streaming_control(STREAM_UVC, CTRL_RESUME, NULL);
                if (resume_ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "camera stream resume failed: %s", esp_err_to_name(resume_ret));
                    break;
                }
            }
            else
            {
                ESP_LOGW(TAG, "UVC: get frame list failed: %s, size = %u", esp_err_to_name(list_ret), frame_list_num);
                break;
            }

            usb_camera.usb_state = 0x01;

            /* 设备连接成功 */
            ESP_LOGI(TAG, "Device connected");
            break;
        /* 关闭连接 */
        case STREAM_DISCONNECTED:
            usb_camera.usb_state = 0x00;
            lv_smail_icon_clear_state(USB_STATE);
            /* 设备断开 */
            ESP_LOGI(TAG, "Device disconnected");
            break;
        default:
            ESP_LOGE(TAG, "Unknown event");
            break;
    }
}

/**
 * @brief       usb摄像头初始化
 * @param       无
 * @retval      无
 */
esp_err_t usb_camera_init(void)
{
    usb_camera.usb_state = 0x00;
    usb_camera.usb_start = 0x00;
    if_ppbuffer_init = false;
    current_width = 0;
    current_height = 0;

    /* 申请USB双缓冲 */
    xfer_buffer_a = (uint8_t *)malloc(DEMO_UVC_XFER_BUFFER_SIZE);
    assert(xfer_buffer_a != NULL);
    xfer_buffer_b = (uint8_t *)malloc(DEMO_UVC_XFER_BUFFER_SIZE);
    assert(xfer_buffer_b != NULL);

    /* mjpeg一帧缓冲 */
    frame_buffer = (uint8_t *)malloc(DEMO_UVC_XFER_BUFFER_SIZE);
    assert(frame_buffer != NULL);

    /* 为ppbuffer_handle句柄申请缓冲 */
    ppbuffer_handle = (PingPongBuffer_t *)calloc(1, sizeof(PingPongBuffer_t));
    assert(ppbuffer_handle != NULL);

    /* 显示摄像头图形 */
    xTaskCreate(usb_display_task, "usb_display_task", 4 * 1024, NULL, 5, NULL);

    /* USB数据流初始化 */
    ESP_ERROR_CHECK(usb_stream_init());

    /* 注册回调函数 */
    ESP_ERROR_CHECK(usb_streaming_state_register(&usb_stream_state_changed_cd, NULL));

    /* 开启USB数据流转输  */
    ESP_ERROR_CHECK(usb_streaming_start());

    return ESP_OK;
}

/**
 * @brief       usb摄像头界面绘画
 * @param       无
 * @retval      无
 */
void usb_camera_ui(void)
{
    if (lv_smail_icon_get_state(USB_STATE) && usb_camera.usb_state != 0x00)
    {
        /* 隐藏box */
        lv_hidden_box();
        
        usb_camera.usb_camera_box = lv_obj_create(lv_scr_act());
        lv_obj_set_style_radius(usb_camera.usb_camera_box, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_border_opa(usb_camera.usb_camera_box,LV_OPA_0,LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(usb_camera.usb_camera_box, lv_color_make(0,0,0), LV_STATE_DEFAULT);
        lv_obj_set_size(usb_camera.usb_camera_box,lv_obj_get_width(lv_scr_act()),lv_obj_get_height(lv_scr_act()) - 20);
        lv_obj_set_pos(usb_camera.usb_camera_box,0,20);
        lv_obj_clear_flag(usb_camera.usb_camera_box, LV_OBJ_FLAG_SCROLLABLE);

        usb_camera.camera_buf.camera_header = lv_img_create(usb_camera.usb_camera_box);
        lv_obj_set_style_bg_color(usb_camera.camera_buf.camera_header, lv_color_make(0,0,0), LV_STATE_DEFAULT);
        lv_obj_center(usb_camera.camera_buf.camera_header);

        usb_camera.camera_buf.usb_pho_btn = lv_btn_create(usb_camera.usb_camera_box);
        lv_obj_set_size(usb_camera.camera_buf.usb_pho_btn,50,50);
        lv_obj_set_style_bg_color(usb_camera.camera_buf.usb_pho_btn,lv_color_make(255,255,255),LV_STATE_DEFAULT);
        lv_obj_set_style_radius(usb_camera.camera_buf.usb_pho_btn,25,LV_STATE_DEFAULT);
        lv_obj_set_style_outline_width(usb_camera.camera_buf.usb_pho_btn,5,LV_STATE_DEFAULT);
        lv_obj_set_style_outline_color(usb_camera.camera_buf.usb_pho_btn,lv_color_make(192,192,192),LV_STATE_DEFAULT);
        lv_obj_set_style_outline_opa(usb_camera.camera_buf.usb_pho_btn,LV_OPA_50,LV_STATE_DEFAULT);
        lv_obj_align(usb_camera.camera_buf.usb_pho_btn,LV_ALIGN_BOTTOM_MID,0,0);
        lv_obj_add_event_cb(usb_camera.camera_buf.usb_pho_btn, lv_phtbtn_control_event_handler, LV_EVENT_ALL, NULL);

        if (indev_keypad != NULL)
        {
            lv_group_add_obj(ctrl_g, usb_camera.camera_buf.usb_pho_btn);
            lv_group_focus_obj(usb_camera.camera_buf.usb_pho_btn);  /* 聚焦第一个APP */
            lv_obj_add_event_cb(usb_camera.camera_buf.usb_pho_btn, lv_phtbtn_control_event_handler, LV_EVENT_ALL, NULL);
        }

        lv_obj_move_foreground(back_btn);
        usb_camera.usb_start = 0x01;
        app_obj_general.del_parent = usb_camera.usb_camera_box;
        app_obj_general.APP_Function = lv_camera_del;
        app_obj_general.app_state = NOT_DEL_STATE;
    }
    else
    {
        lv_msgbox("usb_Camera device not detected");
    }

}

/**
 * @brief       usb摄像头界面退出
 * @param       无
 * @retval      无
 */
void lv_camera_del(void)
{
    usb_camera.usb_start = 0x00;

    /* 删除摄像头父类 */
    if (usb_camera.usb_camera_box != NULL && lv_obj_is_valid(usb_camera.usb_camera_box))
    {
        lv_obj_del(usb_camera.usb_camera_box);
    }
    usb_camera.usb_camera_box = NULL;
    usb_camera.camera_buf.camera_header = NULL;
    usb_camera.camera_buf.usb_pho_btn = NULL;
    /* 显示主界面 */
    lv_display_box();
}
