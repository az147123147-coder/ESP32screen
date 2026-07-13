/**
 ******************************************************************************
 * @file        touch.c
 * @version     V1.0
 * @brief       触摸屏 驱动代码
 * @note        支持电容式触摸屏
 *
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

#include "touch.h"

#define TOUCH_RECOVERY_INTERVAL_MS 5000
#define TOUCH_RECOVERY_TASK_STACK_SIZE 3072

static TaskHandle_t s_touch_recovery_task;

/* 触摸屏控制器初始化参数 */
_m_tp_dev tp_dev =
{
    .init = tp_init,
    .scan = 0,
    .x = {0},
    .y = {0},
    .sta = 0,
    .touchtype = 0x00
};

static esp_err_t tp_try_init(void)
{
    tp_dev.scan = NULL;
    tp_dev.sta = 0;
    tp_dev.touchtype = 0;
    tp_dev.touchtype |= lcd_dev.dir & 0X01;
    esp_err_t ret = gt9xxx_init();
    if (ret != ESP_OK)
    {
        tp_dev.x[0] = 0xFFFF;
        tp_dev.y[0] = 0xFFFF;
        return ret;
    }
    tp_dev.scan = gt9xxx_scan;
    tp_dev.touchtype |= 0X80;
    return ESP_OK;
}

static void tp_recovery_task(void *arg)
{
    (void)arg;

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(TOUCH_RECOVERY_INTERVAL_MS));
        if (tp_dev.scan == NULL && tp_try_init() == ESP_OK)
        {
            ESP_LOGI("TOUCH", "Touch controller recovered");
        }
    }
}

/**
 * @brief       触摸屏初始化
 * @param       无
 * @retval      0,触摸屏初始化成功
 *              1,触摸屏有问题
 */
esp_err_t tp_init(void)
{
    esp_err_t ret = tp_try_init();

    if (s_touch_recovery_task == NULL &&
        xTaskCreatePinnedToCore(tp_recovery_task,
                                "touch_recovery",
                                TOUCH_RECOVERY_TASK_STACK_SIZE,
                                NULL,
                                1,
                                &s_touch_recovery_task,
                                0) != pdPASS)
    {
        s_touch_recovery_task = NULL;
        ESP_LOGE("TOUCH", "Touch recovery task creation failed");
    }

    return ret;
}
