/**
 ******************************************************************************
 * @file        manage.c
 * @version     V1.0
 * @brief       管理
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

#include "manage.h"


atk_list_node_t test_list_head = ATK_LIST_INIT(test_list_head);
uint16_t test_status = 0x00;  /* 记录测试状态 */

/**
 * @brief       创建测试项目
 * @param       pfunc：测试函数入口
 * @retval      返回测试项目控制块
 */
Test_Typedef *test_create(char name[],int (*pfunc)(Test_Typedef * obj))
{
    static int i = 0;
    Test_Typedef * obj = NULL;

    obj = malloc(sizeof(Test_Typedef));                         /* 申请测试实验控制块内存 */
    obj->label = i ++;                                          /* 每一个控制块赋予标号 */
    obj->Function = (void *)pfunc;                              /* 指向测试函数 */
    obj->name_test = strdup(name);                              /* 实验名称 */
    atk_list_add_tail(&test_list_head, &obj->test_list_node);   /* 对象尾部插入列表 */
    return obj;                                                 /* 返回测试函数控制块 */
}

/**
 * @brief       运行测试项目
 * @param       无
 * @retval      无
 */
void test_handler(void)
{
    int status = 0;
    Test_Typedef *data;
    Test_Typedef *data_temp;

    /* 遍历测试项目链表 */
    atk_list_for_each_entry_safe(data, data_temp, &test_list_head, Test_Typedef, test_list_node)
    {
        status = data->Function(data);                      /* 执行测试代码 */

        if (status == TEST_FAIL)
        {
            test_status |= (1 << data->label);              /* 记录测试失败 */
        }
    }

    printf("0x%x\r\n", test_status);
}


#define OTA_PARTITION_LABEL "ota_0" 

esp_err_t execute_bin_file(const char *bin_path) 
{
    esp_err_t err;
    esp_ota_handle_t update_handle = 0;
    const esp_partition_t *update_partition = NULL;

    /* 获取OTA分区 */ 
    update_partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, OTA_PARTITION_LABEL);

    if (!update_partition) 
    {
        ESP_LOGE("ota_example", "Partition not found");
        return ESP_FAIL;
    }

    err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
    if (err != ESP_OK) 
    {
        ESP_LOGE("ota_example", "esp_ota_begin failed! err = %d", err);
        return err;
    }

    FILE *f = fopen(bin_path, "r");
    if (!f) 
    {
        ESP_LOGE("ota_example", "Failed to open binary file");
        ESP_LOGI("ota_example", "Attempting to open file: %s", bin_path);
        esp_ota_end(update_handle);
        return ESP_FAIL;
    }

    char data[1024];
    size_t size;
    while ((size = fread(data, 1, sizeof(data), f)) > 0) 
    {
        err = esp_ota_write(update_handle, data, size);
        if (err != ESP_OK) {
            ESP_LOGE("ota_example", "esp_ota_write failed! err = %d", err);
            fclose(f);
            esp_ota_end(update_handle);
            return err;
        }
    }

    fclose(f);
    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        ESP_LOGE("ota_example", "esp_ota_end failed! err = %d", err);
        return err;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE("ota_example", "esp_ota_set_boot_partition failed! err = %d", err);
        return err;
    }

    esp_restart();

    ESP_LOGE("ota_example", "分区表检验完成，即将跳转。。");

    return ESP_OK;
}
