/**
 ******************************************************************************
 * @file        my_spi.c
 * @version     V1.0
 * @brief       SPI驱动代码
 *****************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

#include "my_spi.h"
#include <string.h>


 /* SD卡设备句柄 */
 spi_device_handle_t MY_SD_Handle = NULL;
 
 /**
  * @brief       spi初始化
  * @param       无
  * @retval      esp_err_t
  */
 esp_err_t my_spi_init(void)
 {
     if (MY_SD_Handle != NULL)
     {
         return ESP_OK;
     }

     spi_bus_config_t buscfg = {
         .sclk_io_num     = SPI_SCLK_PIN,    /* 时钟引脚 */
         .mosi_io_num     = SPI_MOSI_PIN,    /* 主机输出从机输入引脚 */
         .miso_io_num     = SPI_MISO_PIN,    /* 主机输入从机输出引脚 */
         .quadwp_io_num   = -1,              /* 用于Quad模式的WP引脚,未使用时设置为-1 */
         .quadhd_io_num   = -1,              /* 用于Quad模式的HD引脚,未使用时设置为-1 */
         .max_transfer_sz = 320 * 240 * sizeof(uint16_t),   /* 最大传输大小(整屏(RGB565格式)) */
     };
     /* 初始化SPI总线 */
     esp_err_t ret = spi_bus_initialize(MY_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
     if (ret != ESP_OK)
     {
         return ret;
     }
 
     /* SPI驱动接口配置 */
     spi_device_interface_config_t devcfg = {
         .clock_speed_hz = 400 * 1000,        /* SPI时钟 */
         .mode = 0,                          /* SPI模式0 */
         .spics_io_num = GPIO_NUM_NC,        /* 片选引脚 */
         .queue_size = 7,                    /* 事务队列尺寸 7个 */
     };
 
     /* 添加SPI总线设备 */
     ret = spi_bus_add_device(MY_SPI_HOST, &devcfg, &MY_SD_Handle);
     if (ret != ESP_OK)
     {
         spi_bus_free(MY_SPI_HOST);
         return ret;
     }
 
     return ESP_OK;
 }

esp_err_t my_spi_clock_bytes(size_t byte_count)
{
    if (MY_SD_Handle == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    while (byte_count > 0)
    {
        spi_transaction_t transaction = {0};
        size_t chunk = byte_count > sizeof(transaction.tx_data) ? sizeof(transaction.tx_data) : byte_count;
        transaction.flags = SPI_TRANS_USE_TXDATA;
        transaction.length = chunk * 8;
        memset(transaction.tx_data, 0xFF, chunk);

        esp_err_t ret = spi_device_polling_transmit(MY_SD_Handle, &transaction);
        if (ret != ESP_OK)
        {
            return ret;
        }

        byte_count -= chunk;
    }

    return ESP_OK;
}
