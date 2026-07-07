/**
 ******************************************************************************
 * @file        lvgl_demo.c
 * @version     V1.0
 * @brief       lvgl_demo
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

#include "lvgl_demo.h"
#include "demos/lv_demos.h"
#include "spi_sd.h"
#include "tud_sd.h"

static const char *TAG = "wks_lvgl";
lv_indev_data_t touch_data;

#define EXAMPLE_LVGL_TICK_PERIOD_MS     1
#define EXAMPLE_LVGL_TASK_MAX_DELAY_MS  1000
#define EXAMPLE_LVGL_TASK_MIN_DELAY_MS  1
#define EXAMPLE_LVGL_TASK_STACK_SIZE    (8 * 1024)
#define EXAMPLE_LVGL_TASK_PRIORITY      2

lv_indev_t *indev_keypad = NULL;        /* 按键组 */
uint32_t back_act_key = 0;              /* 返回主界面按键 */
uint32_t g_last_key = 0;                /* 记录前一个按键值 */
extern lv_obj_t * back_btn;
extern uint8_t temp[4];
extern TaskHandle_t VIDEOTask_Handler;
extern TaskHandle_t PICTask_Handler;
extern volatile uint8_t app_file_sd_busy;

/* SD_TASK 任务 配置
 * 包括: 任务优先级 堆栈大小 任务句柄 创建任务
 */
#define SD_TASK_PRIO       4                /* 任务优先级 */
#define SD_STK_SIZE        4 * 1024         /* 任务堆栈大小 */
TaskHandle_t SDTask_Handler;                /* 任务句柄 */
void sd_task(void *pvParameters);

#define UART_TASK_PRIO       3
#define UART_STK_SIZE        2 * 1024
TaskHandle_t UARTTask_Handler;
void uart_task(void *pvParameters);

#define KEY_TASK_PRIO       2
#define KEY_STK_SIZE        2 * 1024
TaskHandle_t KEYTask_Handler;
void key_task(void *pvParameters);

static SemaphoreHandle_t lvgl_mux = NULL;

#define TEST_CHAR      'a'
#define SEND_INTERVAL  1000
#define READ_TIMEOUT   50

void key_task(void *pvParameters)
{
    pvParameters = pvParameters;
    uint8_t key;

    while(1)
    {
        key = key_scan(0);

        switch (key)
        {
            case BOOT_PRES:
            {
                LED1_TOGGLE();
                break;
            }
            default:
            {
                break;
            }
        }

        vTaskDelay(10);
    }
}
/**
 * @brief       uart
 * @param       pvParameters : 传入参数(未用到)
 * @retval      无
 */
void uart_task(void *pvParameters)
{
    pvParameters = pvParameters;
    uint8_t *rx_buffer = malloc(RX_BUF_SIZE);  

	while(1) 
	{
        /* 发送测试字符 */
		char tx_char = 'a'; 
		uart_write_bytes(USART_UX, &tx_char, 1);
        
        /* 等待数据完全发送 */
        uart_wait_tx_done(USART_UX, pdMS_TO_TICKS(50));

        /* 读取回传数据 */
        int len = uart_read_bytes(USART_UX, rx_buffer, RX_BUF_SIZE, pdMS_TO_TICKS(READ_TIMEOUT));
        
        /* 检测接收状态 */
        if (len > 0) 
		{
            /* 遍历接收缓冲区查找目标字符 */ 
            for (int i = 0; i < len; i++) 
			{
                if (rx_buffer[i] == TEST_CHAR) 
				{
                    LED1(0);  
                    break;
                } 
				else 
				{
                    LED1(1);  
                }
            }
        } 
		else 
		{
            LED1(1);  /* 无数据时熄灭LED */ 
        }

        /* 清空接收缓冲区 */
        uart_flush_input(USART_UX);
        
        vTaskDelay(pdMS_TO_TICKS(SEND_INTERVAL));
    }
	free(rx_buffer);
}

/**
 * @brief       sd
 * @param       pvParameters : 传入参数(未用到)
 * @retval      无
 */
void sd_task(void *pvParameters)
{
    pvParameters = pvParameters;
    uint32_t poll_tick = 0;
    uint8_t sd_present_last = lv_smail_icon_get_state(TF_STATE);

    while(1)
    {
        if (VIDEOTask_Handler != NULL || PICTask_Handler != NULL || app_file_sd_busy || (g_usbdev.status & 0x01))
        {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        if(fs_open_flag == 0 || fs_read_flag == 0 || fs_dir_open_flag == 0 || fs_dir_read_flag == 0)
        {
            SD_CS(0);
        }
        else if(fs_open_flag == 1 && fs_read_flag == 1 && fs_dir_open_flag == 1 && fs_dir_read_flag == 1)
        {
            SD_CS(1);
        }

        poll_tick += 500;

        if (poll_tick >= 1000)
        {
            poll_tick = 0;

            if (sd_card_is_mounted())
            {
                if (sd_card_check() != ESP_OK)
                {
                    lv_smail_icon_clear_state(TF_STATE);
                    if (sd_present_last)
                    {
                        lv_sd_toast("SD removed");
                    }
                    sd_present_last = 0;

                    if (app_obj_general.del_parent != NULL && app_obj_general.APP_Function != NULL)
                    {
                        app_obj_general.app_state = DEL_STATE;
                    }

                    for (uint8_t i = 0; i < 60; i++)
                    {
                        if (app_obj_general.del_parent == NULL)
                        {
                            break;
                        }

                        vTaskDelay(pdMS_TO_TICKS(50));
                    }

                    sd_card_unmount();
                }
                else
                {
                    lv_smail_icon_add_state(TF_STATE);
                    sd_present_last = 1;
                }
            }
            else
            {
                if (sd_card_mount() == ESP_OK)
                {
                    lv_smail_icon_add_state(TF_STATE);
                    if (!sd_present_last)
                    {
                        lv_sd_toast("SD inserted");
                    }
                    sd_present_last = 1;
                }
                else
                {
                    lv_smail_icon_clear_state(TF_STATE);
                    sd_present_last = 0;
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/**
 * @brief       lvgl刷新回调函数
 * @param       drv         :lcd设备
 * @param       area        :绘画区域
 * @param       color_map   :绘画颜色
 * @retval      无
 */
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
	int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
    esp_lcd_panel_draw_bitmap((esp_lcd_panel_handle_t) drv->user_data, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
    lv_disp_flush_ready(drv);
}

/**
 * @brief       提供LVGL节拍
 * @param       arg         :转入参数（未使用）
 * @retval      无
 */
static void lvgl_increase_tick(void *arg)
{
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

/**
 * @brief       进入互斥锁
 * @param       timeout_ms         :等待时间
 * @retval      
 */
bool lvgl_mux_lock(int timeout_ms)
{
    if (lvgl_mux == NULL)
    {
        return false;
    }

    const TickType_t timeout_ticks = (timeout_ms == -1) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(lvgl_mux, timeout_ticks) == pdTRUE;
}

/**
 * @brief       释放互斥锁
 * @param       无
 * @retval      无
 */
void lvgl_mux_unlock(void)
{
    if (lvgl_mux == NULL)
    {
        return;
    }

    xSemaphoreGiveRecursive(lvgl_mux);
}

/**
 * @brief       释放互斥锁
 * @param       无
 * @retval      无
 */
static void lvgl_port_task(void *arg)
{
    arg = arg;
    uint32_t task_delay_ms = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;

    while (1)
    {
        /* 锁定互斥锁,因为LVGL api不是线程安全的*/
        if (lvgl_mux_lock(-1))
        {
            task_delay_ms = lv_timer_handler();
            /* 使用互斥锁 */
            lvgl_mux_unlock();
        }
        if (task_delay_ms > EXAMPLE_LVGL_TASK_MAX_DELAY_MS)
        {
            task_delay_ms = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;
        }
        else if (task_delay_ms < EXAMPLE_LVGL_TASK_MIN_DELAY_MS)
        {
            task_delay_ms = EXAMPLE_LVGL_TASK_MIN_DELAY_MS;
        }

        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }
}

/**
 * @brief       获取触摸屏状态
 * @param       无
 * @retval      true:有触摸，false:无触摸
 */
static bool touchpad_is_pressed(void)
{
    tp_dev.scan(0);

    if (tp_dev.sta & TP_PRES_DOWN)
    {
        return true;
    }

    return false;
}

/**
 * @brief       读取坐标
 * @param       x:x轴坐标
 * @param       y:y轴坐标
 * @retval      无
 */
void touchpad_get_xy(lv_coord_t *x, lv_coord_t *y)
{
    (*x) = tp_dev.x[0];
    (*y) = tp_dev.y[0];
}

/**
 * @brief       坐标读取
 * @param       indev_drv:输入设备句柄
 * @param       data:输入设备数据存储
 * @retval      无
 */
void touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
	assert(indev_drv); /* 确保驱动程序有效 */
    /* 从触摸控制器读取数据到内存 */
    tp_dev.scan(0); /* 扫描触摸数据 */

    if (tp_dev.sta & TP_PRES_DOWN) /* 检查触摸是否按下 */
    {
        data->point.x = tp_dev.x[0]; /* 获取触摸点X坐标 */
        data->point.y = tp_dev.y[0]; /* 获取触摸点Y坐标 */
        data->state = LV_INDEV_STATE_PRESSED; /* 设置状态为按下 */
    }
    else
    {
        data->state = LV_INDEV_STATE_RELEASED; /* 设置状态为释放 */
    }

	touch_data = *data;
}

static uint32_t keypad_get_key(void);
/**
 * @brief       图形库的键盘读取回调函数
 * @param       indev_drv : 键盘设备
 * @arg         data      : 输入设备数据结构体
 * @retval      无
 */
static void keypad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    /* 获取按键是否被按下，并保存键值 */
    uint32_t act_key = keypad_get_key();

    if(act_key != 0) 
    {
        data->state = LV_INDEV_STATE_PR;

        /* 将键值转换成 LVGL 的控制字符 */
        switch(act_key)
        {
            case BOOT_PRES:
                act_key = BOOT_PRES;
                back_act_key = BOOT_PRES;
                lv_event_send(back_btn,LV_EVENT_CLICKED,NULL);
            
            break;
        }
        g_last_key = act_key;
    }
    else 
    {
        data->state = LV_INDEV_STATE_REL;
        g_last_key = 0;
    }

    data->key = g_last_key;
}

/**
 * @brief       通过IO扩展芯片获取当前正在按下的按键
 * @param       无
 * @retval      0 : 按键没有被按下
 */
static uint32_t keypad_get_key(void)
{
    uint8_t key1;
    uint8_t key2;

    key2 = key_scan(0);

    if (BOOT_PRES == key2)
    {
        return key2;
    } 

    return 0;
}


/**
 * @brief       初始化触摸设备
 * @param       无
 * @retval      无
 */
void lv_port_indev_init(void)
{
    static lv_indev_drv_t indev_drv; 
    tp_dev.init();

	/* 注册触摸输入设备 */
	lv_indev_drv_init(&indev_drv);
	indev_drv.type = LV_INDEV_TYPE_POINTER;
	indev_drv.read_cb = touchpad_read;
	lv_indev_drv_register(&indev_drv);
}

lv_disp_drv_t disp_drv;      /* 回调函数的参数 */

/**
 * @brief       lvgl程序入口
 * @param       无
 * @retval      无
 */
void lvgl_demo(void)
{
    static lv_disp_draw_buf_t disp_buf; /* 绘画区域的存储区 */
       
    if (sd_spi_init() == ESP_OK)        /* 初始化TF卡 */
    {
        lv_smail_icon_add_state(TF_STATE);
    }
    else
    {
        lv_smail_icon_clear_state(TF_STATE);
    }

	/* 创建SD卡 CS引脚控制任务 */
    xTaskCreatePinnedToCore((TaskFunction_t )sd_task,               /* 任务函数 */
                            (const char*    )"sd_task",             /* 任务名称 */
                            (uint16_t       )SD_STK_SIZE,           /* 任务堆栈大小 */
                            (void*          )NULL,                  /* 传入给任务函数的参数 */
                            (UBaseType_t    )SD_TASK_PRIO,          /* 任务优先级 */
                            (TaskHandle_t*  )&SDTask_Handler,       /* 任务句柄 */
                            (BaseType_t     ) 0);                   /* 该任务哪个内核运行 */	

	/* 创建UART控制任务 */
    xTaskCreatePinnedToCore((TaskFunction_t )uart_task,             /* 任务函数 */
							(const char*    )"uart_task",           /* 任务名称 */
							(uint16_t       )UART_STK_SIZE,         /* 任务堆栈大小 */
							(void*          )NULL,                  /* 传入给任务函数的参数 */
							(UBaseType_t    )UART_TASK_PRIO,        /* 任务优先级 */
							(TaskHandle_t*  )&UARTTask_Handler,     /* 任务句柄 */
							(BaseType_t     ) 0);                   /* 该任务哪个内核运行 */

	/* 创建KEY控制任务 */
    xTaskCreatePinnedToCore((TaskFunction_t )key_task,               /* 任务函数 */
                            (const char*    )"key_task",             /* 任务名称 */
                            (uint16_t       )KEY_STK_SIZE,           /* 任务堆栈大小 */
                            (void*          )NULL,                   /* 传入给任务函数的参数 */
                            (UBaseType_t    )KEY_TASK_PRIO,          /* 任务优先级 */
                            (TaskHandle_t*  )&KEYTask_Handler,       /* 任务句柄 */
                            (BaseType_t     ) 0);                    /* 该任务哪个内核运行 */							
						
    lv_init();                          /* 初始化lvgl */

	void *buf1 = NULL;
    void *buf2 = NULL;

	buf1 = heap_caps_malloc(lcd_dev.width * 60 * sizeof(lv_color_t), MALLOC_CAP_DMA);
    buf2 = heap_caps_malloc(lcd_dev.width * 60 * sizeof(lv_color_t), MALLOC_CAP_DMA);

    /* 输出内存地址 */
    ESP_LOGI(TAG, "buf1@%p, buf2@%p", buf1, buf2);
    /* 初始化绘画存储区 */
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, lcd_dev.width * 60);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = lcd_dev.width;
    disp_drv.ver_res = lcd_dev.height;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.user_data = panel_handle;
    /* 显示设备注册 */
    lv_disp_drv_register(&disp_drv);
    /* 触摸设备注册 */
    lv_port_indev_init();

    /* 创建定时器提供lvgl时钟节拍 */
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &lvgl_increase_tick,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));

    lvgl_mux = xSemaphoreCreateRecursiveMutex();
    assert(lvgl_mux);

    xTaskCreatePinnedToCore(lvgl_port_task, "LVGL", EXAMPLE_LVGL_TASK_STACK_SIZE, NULL, EXAMPLE_LVGL_TASK_PRIORITY, NULL, 0);

    /* 锁定互斥锁，因为LVGL api不是线程安全的 */
    if (lvgl_mux_lock(-1))
    {
         lv_start_ui();
        /* 释放互斥锁 */
        lvgl_mux_unlock();
    }
}
