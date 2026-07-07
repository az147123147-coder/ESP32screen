/**
 ******************************************************************************
 * @file        manage.h
 * @version     V1.0
 * @brief       管理
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

#ifndef __MANAGE_H
#define __MANAGE_H

#include "list.h"
#include <stdint.h>
#include <stddef.h>
#include "esp_timer.h"
#include "esp_image_format.h"
#include "esp_ota_ops.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "ff.h"


/* 测试实验状态 */
enum STATE
{
    TEST_OK,
    TEST_FAIL
};

/* 菜单结构体 */
typedef struct Test
{
    atk_list_node_t test_list_node;     /* 父类链表的节点 */
    char *name_test;                    /* 实验名称 */
    uint8_t label;                      /* 标号 */
    int (*Function)(void * widget);     /* 测试函数 */
}Test_Typedef;

extern uint16_t test_status;            /* 导出变量 */

/* 函数声明 */
Test_Typedef *test_create(char name[],int (*pfunc)(Test_Typedef * obj));
void test_handler(void);
esp_err_t load_and_run_firmware(const char *image_path);
esp_err_t execute_bin_file(const char *bin_path);

#endif
