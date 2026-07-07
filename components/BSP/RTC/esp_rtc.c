/**
 ******************************************************************************
 * @file        esp_rtc.c
 * @version     V1.0
 * @brief       RTC驱动代码
 *****************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

#include "esp_rtc.h"
#include "nvs.h"
#include "esp_sntp.h"
#include <time.h>
#include <stdlib.h>


_calendar_obj calendar;         /* 时间结构体 */

#define RTC_NVS_NAMESPACE "rtc_time"
#define RTC_NVS_KEY_LAST_TIME "last_time"
#define RTC_MIN_VALID_TIME 1704067200LL

static void rtc_save_time_task(void *arg)
{
    rtc_save_current_time();
    vTaskDelete(NULL);
}

static void rtc_sntp_sync_cb(struct timeval *tv)
{
    if (tv != NULL && tv->tv_sec >= RTC_MIN_VALID_TIME)
    {
        xTaskCreate(rtc_save_time_task, "rtc_save", 2048, NULL, 3, NULL);
    }
}

void rtc_time_init(void)
{
    setenv("TZ", "CST-8", 1);
    tzset();
}

bool rtc_restore_saved_time(void)
{
    nvs_handle_t handle;
    int64_t saved_time = 0;

    if (nvs_open(RTC_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK)
    {
        return false;
    }

    esp_err_t err = nvs_get_i64(handle, RTC_NVS_KEY_LAST_TIME, &saved_time);
    nvs_close(handle);

    if (err != ESP_OK || saved_time < RTC_MIN_VALID_TIME)
    {
        return false;
    }

    struct timeval val = { .tv_sec = (time_t)saved_time, .tv_usec = 0 };
    return settimeofday(&val, NULL) == 0;
}

bool rtc_save_current_time(void)
{
    time_t now;
    nvs_handle_t handle;

    time(&now);
    if (now < RTC_MIN_VALID_TIME)
    {
        return false;
    }

    if (nvs_open(RTC_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
    {
        return false;
    }

    esp_err_t err = nvs_set_i64(handle, RTC_NVS_KEY_LAST_TIME, (int64_t)now);
    if (err == ESP_OK)
    {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err == ESP_OK;
}

void rtc_start_sntp_sync(void)
{
    rtc_time_init();

    if (esp_sntp_enabled())
    {
        esp_sntp_stop();
    }

    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    esp_sntp_set_time_sync_notification_cb(rtc_sntp_sync_cb);
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_init();
}

/**
 * @brief       RTC设置时间
 * @param       year    :年
 * @param       mon     :月
 * @param       mday    :日
 * @param       hour    :时
 * @param       min     :分
 * @param       sec     :秒
 * @retval      无
 */
void rtc_set_time(int year,int mon,int mday,int hour,int min,int sec)
{
    struct tm datetime;
    /* 设置时间 */
    datetime.tm_year = year - 1900;
    datetime.tm_mon = mon - 1;
    datetime.tm_mday = mday;
    datetime.tm_hour = hour;
    datetime.tm_min = min;
    datetime.tm_sec = sec;
    datetime.tm_isdst = -1;
    /* 获取1970.1.1以来的总秒数 */
    time_t second = mktime(&datetime);
    struct timeval val = { .tv_sec = second, .tv_usec = 0 };
    /* 设置当前时间 */
    settimeofday(&val, NULL);
}

/**
 * @brief       获取当前的时间
 * @param       无
 * @retval      无
 */
void rtc_get_time(void)
{
    struct tm *datetime;
    time_t second;
    /* 返回自(1970.1.1 00:00:00 UTC)经过的时间(秒) */
    time(&second);
    datetime = localtime(&second);

    calendar.hour = datetime->tm_hour;          /* 时 */
    calendar.min = datetime->tm_min;            /* 分 */
    calendar.sec = datetime->tm_sec;            /* 秒 */
    /* 公历年月日周 */
    calendar.year = datetime->tm_year + 1900;   /* 年 */
    calendar.month = datetime->tm_mon + 1;      /* 月 */
    calendar.date = datetime->tm_mday;          /* 日 */
    /* 周 */
    calendar.week = rtc_get_week(calendar.year, calendar.month, calendar.date);
}

/**
 * @brief       将年月日时分秒转换成秒钟数
 *   @note      输入公历日期得到星期(起始时间为: 公元0年3月1日开始, 输入往后的任何日期, 都可以获取正确的星期)
 *              使用 基姆拉尔森计算公式 计算, 原理说明见此贴:
 *              https://www.cnblogs.com/fengbohello/p/3264300.html
 * @param       syear : 年份
 * @param       smon  : 月份
 * @param       sday  : 日期
 * @retval      0, 星期天; 1 ~ 6: 星期一 ~ 星期六
 */
uint8_t rtc_get_week(uint16_t year, uint8_t month, uint8_t day)
{
    uint8_t week = 0;

    if (month < 3)
    {
        month += 12;
        --year;
    }

    week = (day + 1 + 2 * month + 3 * (month + 1) / 5 + year + (year >> 2) - year / 100 + year / 400) % 7;
    return week;
}

