/**
 ******************************************************************************
 * @file        xl9555.c
 * @version     V1.0
 * @brief       XL9555驱动代码
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

#include "xl9555.h"


const char *xl9555_tag = "xl9555";
i2c_master_dev_handle_t xl9555_handle = NULL;

/**
 * @brief       读取XL9555的IO值
 * @param       data:读取数据的存储区
 * @param       len:读取数据的大小
 * @retval      ESP_OK:读取成功; 其他:读取失败
 */
esp_err_t xl9555_read_byte(uint8_t *data, size_t len)
{
    uint8_t reg_addr = XL9555_OUTPUT_PORT0_REG;
    
    return i2c_master_transmit_receive(xl9555_handle, &reg_addr, 1, data, len, -1);
}

/**
 * @brief       向XL9555寄存器写入数据
 * @param       reg:寄存器地址
 * @param       data:要写入数据的存储区
 * @param       len:要写入数据的大小
 * @retval      ESP_OK:读取成功; 其他:读取失败
 */
esp_err_t xl9555_write_byte(uint8_t reg, uint8_t *data, size_t len)
{
    esp_err_t ret;

    uint8_t *buf = malloc(1 + len);
    if (buf == NULL)
    {
        ESP_LOGE(xl9555_tag, "%s memory failed", __func__);
        return ESP_ERR_NO_MEM;      /* 分配内存失败 */
    }

    buf[0] = reg;                   /* 0号元素为寄存器数值 */
    memcpy(buf + 1, data, len);     /* 拷贝数据至存储区中 */

    ret = i2c_master_transmit(xl9555_handle, buf, len + 1, -1);

    free(buf);                      /* 发送完成释放内存 */

    return ret;
}

/**
 * @brief       控制某个IO的电平
 * @param       pin     : 控制的IO
 * @param       val     : 电平
 * @retval      返回所有IO状态
 */
uint16_t xl9555_pin_write(uint16_t pin, int val)
{
    uint8_t w_data[2];
    uint16_t temp = 0x0000;

    xl9555_read_byte(w_data, 2);

    if (pin <= 0x0080)
    {
        if (val)
        {
            w_data[0] |= (uint8_t)(0xFF & pin);
        }
        else
        {
            w_data[0] &= ~(uint8_t)(0xFF & pin);
        }
    }
    else
    {
        if (val)
        {
            w_data[1] |= (uint8_t)(0xFF & (pin >> 8));
        }
        else
        {
            w_data[1] &= ~(uint8_t)(0xFF & (pin >> 8));
        }
    }

    temp = ((uint16_t)w_data[1] << 8) | w_data[0]; 

    xl9555_write_byte(XL9555_OUTPUT_PORT0_REG, w_data, 2);
    
    return temp;
}

/**
 * @brief       获取某个IO状态
 * @param       pin : 要获取状态的IO
 * @retval      此IO口的值(状态, 0/1)
 */
int xl9555_pin_read(uint16_t pin)
{
    uint16_t ret;
    uint8_t r_data[2];

    xl9555_read_byte(r_data, 2);

    ret = r_data[1] << 8 | r_data[0];

    return (ret & pin) ? 1 : 0;
}

/**
 * @brief       XL9555的IO配置
 * @param       config_value：IO配置输入或者输出
 * @retval      返回设置的数值
 */
void xl9555_ioconfig(uint16_t config_value)
{
    /* 从机地址 + CMD + data1(P0) + data2(P1) */
    /* P10、P11、P12、P13和P14为输入，其他引脚为输出 -->0001 1111 0000 0000 注意：0为输出，1为输入*/
    uint8_t data[2];
    esp_err_t err;

    data[0] = (uint8_t)(0xFF & config_value);
    data[1] = (uint8_t)(0xFF & (config_value >> 8));

    do
    {
        err = xl9555_write_byte(XL9555_CONFIG_PORT0_REG, data, 2);
        if (err != ESP_OK)
        {
            ESP_LOGE(xl9555_tag, "%s configure %X failed, ret: %d", __func__, config_value, err);
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
        
    } while (err != ESP_OK);
}

/**
 * @brief       初始化XL9555
 * @param       无
 * @retval      ESP_OK:初始化成功
 */
esp_err_t xl9555_init(void)
{
    uint8_t r_data[2];

    /* 未调用myiic_init初始化IIC */
    if (bus_handle == NULL)
    {
        ESP_ERROR_CHECK(myiic_init());
    }

    i2c_device_config_t xl9555_i2c_dev_conf = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,  /* 从机地址长度 */
        .scl_speed_hz    = IIC_SPEED_CLK,       /* 传输速率 */
        .device_address  = XL9555_ADDR,         /* 从机7位的地址 */
    };
    /* I2C总线上添加XL9555设备 */
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &xl9555_i2c_dev_conf, &xl9555_handle));

    /* 上电先读取一次清除中断标志 */
    xl9555_read_byte(r_data, 2);
    /* 配置那些扩展管脚为输入输出模式 */
    xl9555_ioconfig(0xF003);

    return ESP_OK;
}

/**
 * @brief       翻转某个io的电平
 * @param       pin : 要翻转的io
 * @retval      ESP_OK:成功; 其他:失败
 */
esp_err_t xl9555_pin_toggle(uint16_t pin)
{
    uint8_t output_reg[2];
    esp_err_t ret;
    
    /* 读取当前输出状态 */
    ret = xl9555_read_byte(output_reg, 2);
    if (ret != ESP_OK) return ret;

    /* 合并为16位并执行异或操作 */
    uint16_t current_state = (output_reg[1] << 8) | output_reg[0];
    current_state ^= pin;
    
    /* 分解并写回新状态 */
    output_reg[0] = current_state & 0xFF;
    output_reg[1] = (current_state >> 8) & 0xFF;
    
    return xl9555_write_byte(XL9555_OUTPUT_PORT0_REG, output_reg, 2);
}
