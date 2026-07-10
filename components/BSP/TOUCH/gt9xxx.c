/**
 ******************************************************************************
 * @file        gt9xxx.c
 * @version     V1.0
 * @brief       电容触摸屏-GT9xxx 驱动代码
 *   @note      GT系列电容触摸屏IC通用驱动,本代码支持: GT9147/GT911/GT928/GT1151/GT9271 等多种
 *              驱动IC, 这些驱动IC仅ID不一样, 具体代码基本不需要做任何修改即可通过本代码直接驱动
 *
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

#include "gt9xxx.h"


const char * gt9xxx_tag = "gt9xxx";
i2c_master_bus_handle_t myiic1_bus_handle;     /* 总线句柄 */
i2c_master_dev_handle_t gt9xxx_handle = NULL;
#define GT9XXX_I2C_TIMEOUT_MS 100

/* 注意: 除了GT9271支持10点触摸之外, 其他触摸芯片只支持 5点触摸 */
uint8_t g_gt_tnum = 5;      /* 默认支持的触摸屏点数(5点触摸) */

/**
 * @brief       向gt9xxx写入数据
 * @param       reg : 起始寄存器地址
 * @param       buf : 数据缓缓存区
 * @param       len : 写数据长度
 * @retval      esp_err_t ：0, 成功; 1, 失败;
 */
esp_err_t gt9xxx_wr_reg(uint16_t reg, uint8_t *buf, uint8_t len)
{
    if (gt9xxx_handle == NULL || (buf == NULL && len != 0))
    {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret;
    uint8_t *wr_buf = malloc(2 + len);

    if (wr_buf == NULL)
    {
        ESP_LOGE(gt9xxx_tag, "%s memory failed", __func__);
        return ESP_ERR_NO_MEM;      /* 分配内存失败 */
    }

    wr_buf[0] = reg >> 8;
    wr_buf[1] = reg & 0XFF;

    memcpy(wr_buf + 2, buf, len);     /* 拷贝数据至存储区当中 */

    ret = i2c_master_transmit(gt9xxx_handle, wr_buf, 2 + len, GT9XXX_I2C_TIMEOUT_MS);

    free(wr_buf);                      /* 发送完成释放内存 */

    return ret;
}

/**
 * @brief       从gt9xxx读出数据
 * @param       reg : 起始寄存器地址
 * @param       buf : 数据缓缓存区
 * @param       len : 读数据长度
 * @retval      esp_err_t ：0, 成功; 1, 失败;
 */
esp_err_t gt9xxx_rd_reg(uint16_t reg, uint8_t *buf, uint8_t len)
{
    if (gt9xxx_handle == NULL || buf == NULL || len == 0)
    {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t memaddr_buf[2];
    memaddr_buf[0]  = reg >> 8;
    memaddr_buf[1]  = reg & 0XFF;

    return i2c_master_transmit_receive(gt9xxx_handle, memaddr_buf, sizeof(memaddr_buf), buf, len, GT9XXX_I2C_TIMEOUT_MS);
}

/**
 * @brief       初始化MYIIC1
 * @param       无
 * @retval      ESP_OK:初始化成功
 */
esp_err_t myiic1_init(void)
{
    if (myiic1_bus_handle != NULL)
    {
        return ESP_OK;
    }

    i2c_master_bus_config_t i2c1_bus_config = {
        .clk_source                     = I2C_CLK_SRC_DEFAULT,  /* 时钟源 */
        .i2c_port                       = I2C_NUM_1,            /* I2C端口 */
        .scl_io_num                     = GPIO_NUM_40,          /* SCL管脚 */
        .sda_io_num                     = GPIO_NUM_41,          /* SDA管脚 */
        .glitch_ignore_cnt              = 7,                    /* 故障周期 */
        .flags.enable_internal_pullup   = true,                 /* 内部上拉 */
    };
    /* 新建I2C总线 */
    return i2c_new_master_bus(&i2c1_bus_config, &myiic1_bus_handle);
}

/**
 * @brief       初始化gt9xxx触摸屏
 * @param       无
 * @retval      0, 初始化成功; 1, 初始化失败;
 */
esp_err_t gt9xxx_init(void)
{
    uint8_t temp[5] = {0};

    esp_err_t ret = myiic1_init();                                  /* IIC1初始化 */
    if (ret != ESP_OK)
    {
        return ret;
    }

    i2c_device_config_t gt9xxx_i2c_dev_conf = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,      /* 从机地址长度 */
        .scl_speed_hz    = 400000,                  /* 传输速率 */
        .device_address  = GT9XXX_DEV_ID,           /* 从机7位的地址 */
    };
    /* I2C总线上添加gt9xxx设备 */
    ret = i2c_master_bus_add_device(myiic1_bus_handle, &gt9xxx_i2c_dev_conf, &gt9xxx_handle);
    if (ret != ESP_OK)
    {
        return ret;
    }

    /* 以下是对CT_RST和CT_INT引脚做配置,同时会产生的时序决定了驱动IC的器件地址为0x14(某些芯片还会有另外一种时序,会使器件地址为0x5D) */
    gpio_config_t gpio_init_struct = {0};
    gpio_init_struct.intr_type    = GPIO_INTR_DISABLE;              /* 失能引脚中断 */
    gpio_init_struct.pull_up_en   = GPIO_PULLUP_DISABLE;            /* 失能上拉 */
    gpio_init_struct.pull_down_en = GPIO_PULLDOWN_DISABLE;          /* 失能下拉 */
    gpio_init_struct.mode         = GPIO_MODE_INPUT;                /* 输入模式 */
    gpio_init_struct.pin_bit_mask = 1ull << GT9XXX_INT_GPIO_PIN;    /* 设置的引脚的位掩码 */
    gpio_config(&gpio_init_struct);                                 /* 配置中断引脚 */

    for (int i = 2;i > 0;i--)
    {
        CT_RST(0);
        vTaskDelay(pdMS_TO_TICKS(200));
        CT_RST(1);
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    ret = gt9xxx_rd_reg(GT9XXX_PID_REG, temp, 4);     /* 读取产品ID */
    if (ret != ESP_OK)
    {
        return ret;
    }
    temp[4] = 0;
    /* 判断一下是否是特定的触摸屏 */
    if (strcmp((char *)temp, "911") && strcmp((char *)temp, "9147") && strcmp((char *)temp, "1158") && strcmp((char *)temp, "9271") && strcmp((char *)temp, "928"))
    {
        return 1;   /* 若不是触摸屏用到的GT911/9147/1158/9271，则初始化失败，需硬件查看触摸IC型号以及查看时序函数是否正确 */
    }
    ESP_LOGI("GT9XXX", "CTP:%s", temp);         /* 打印触摸屏驱动IC的ID */                      
    
    if (strcmp((char *)temp, "9271") == 0)      /* ID==9271 / ID==928, 支持10点触摸 */
    {
        g_gt_tnum = 10;                         /* 支持10点触摸屏 */
    }

    temp[0] = 0X02;
    ret = gt9xxx_wr_reg(GT9XXX_CTRL_REG, temp, 1);    /* 软复位GT9XXX */
    if (ret != ESP_OK)
    {
        return ret;
    }
    
    vTaskDelay(10);
    
    temp[0] = 0X00;
    return gt9xxx_wr_reg(GT9XXX_CTRL_REG, temp, 1);    /* 结束复位, 进入读坐标状态 */
}

static void gt9xxx_release_touch(void)
{
    tp_dev.sta = 0;
    for (uint8_t i = 0; i < CT_MAX_TOUCH; i++)
    {
        tp_dev.x[i] = 0xFFFF;
        tp_dev.y[i] = 0xFFFF;
    }
}

/* GT9XXX 10个触摸点(最多) 对应的寄存器表 */
const uint16_t GT9XXX_TPX_TBL[10] =
{
    GT9XXX_TP1_REG, GT9XXX_TP2_REG, GT9XXX_TP3_REG, GT9XXX_TP4_REG, GT9XXX_TP5_REG,
    GT9XXX_TP6_REG, GT9XXX_TP7_REG, GT9XXX_TP8_REG, GT9XXX_TP9_REG, GT9XXX_TP10_REG,
};

/**
 * @brief       扫描触摸屏(采用查询方式)
 * @param       mode : 电容屏未用到次参数, 为了兼容电阻屏
 * @retval      当前触屏状态
 *   @arg       0, 触屏无触摸; 
 *   @arg       1, 触屏有触摸;
 */
uint8_t gt9xxx_scan(uint8_t mode)
{
    uint8_t buf[4];
    uint8_t i = 0;
    uint8_t res = 0;
    uint16_t temp;
    uint16_t tempsta;
    static uint8_t t = 0;           /* 控制查询间隔,从而降低CPU占用率 */
    t++;

    if ((t % 10) == 0 || t < 10)    /* 空闲时,每进入10次CTP_Scan函数才检测1次,从而节省CPU使用率 */
    {
        if (gt9xxx_rd_reg(GT9XXX_GSTID_REG, &mode, 1) != ESP_OK)              /* 读取触摸点的状态 */
        {
            gt9xxx_release_touch();
            return 0;
        }

        if ((mode & 0X80) && ((mode & 0XF) <= g_gt_tnum))
        {
            i = 0;
            if (gt9xxx_wr_reg(GT9XXX_GSTID_REG, &i, 1) != ESP_OK)             /* 清标志 */
            {
                gt9xxx_release_touch();
                return 0;
            }
        }

        if ((mode & 0XF) && ((mode & 0XF) <= g_gt_tnum))
        {
            temp = 0XFFFF << (mode & 0XF);                      /* 将点的个数转换为1的位数,匹配tp_dev.sta定义 */
            tempsta = tp_dev.sta;                               /* 保存当前的tp_dev.sta值 */
            tp_dev.sta = (~temp) | TP_PRES_DOWN | TP_CATH_PRES;
            tp_dev.x[g_gt_tnum - 1] = tp_dev.x[0];              /* 保存触点0的数据,保存在最后一个上 */
            tp_dev.y[g_gt_tnum - 1] = tp_dev.y[0];

            for (i = 0; i < g_gt_tnum; i++)
            {
                if (tp_dev.sta & (1 << i))                      /* 触摸有效? */
                {
                    if (gt9xxx_rd_reg(GT9XXX_TPX_TBL[i], buf, 4) != ESP_OK)   /* 读取XY坐标值 */
                    {
                        gt9xxx_release_touch();
                        return 0;
                    }

                    if (lcd_dev.dir == 0)        /* 竖屏 */
                    {
                        tp_dev.x[i] = ((uint16_t)buf[1] << 8) + buf[0];
                        tp_dev.y[i] = ((uint16_t)buf[3] << 8) + buf[2];
                    }
                    else                        /* 横屏 */
                    {
                        tp_dev.x[i] = lcd_dev.width - 1 - (((uint16_t)buf[3] << 8) + buf[2]);
                        tp_dev.y[i] = ((uint16_t)buf[1] << 8) + buf[0];
                    }
                }

                 //ESP_LOGI("GT9XXX", "x[%d]:%d,y[%d]:%d\r\n", i, tp_dev.x[i], i, tp_dev.y[i]);
            }

            res = 1;

            if (tp_dev.x[0] >= lcd_dev.width || tp_dev.y[0] >= lcd_dev.height)      /* 非法数据(坐标超出了) */
            {
                if ((mode & 0XF) > 1)                   /* 有其他点有数据,则复第二个触点的数据到第一个触点. */
                {
                    tp_dev.x[0] = tp_dev.x[1];
                    tp_dev.y[0] = tp_dev.y[1];
                    t = 0;                              /* 触发一次,则会最少连续监测10次,从而提高命中率 */
                }
                else                                    /* 非法数据,则忽略此次数据(还原原来的) */
                {
                    tp_dev.x[0] = tp_dev.x[g_gt_tnum - 1];
                    tp_dev.y[0] = tp_dev.y[g_gt_tnum - 1];
                    mode = 0X80;
                    tp_dev.sta = tempsta;               /* 恢复tp_dev.sta */
                }
            }
            else 
            {
                t = 0;                                  /* 触发一次,则会最少连续监测10次,从而提高命中率 */
            }
        }
    }

    if ((mode & 0X8F) == 0X80)              /* 无触摸点按下 */
    {
        if (tp_dev.sta & TP_PRES_DOWN)      /* 之前是被按下的 */
        {
            tp_dev.sta &= ~TP_PRES_DOWN;    /* 标记按键松开 */
        }
        else                                /* 之前就没有被按下 */
        {
            tp_dev.x[0] = 0xffff;
            tp_dev.y[0] = 0xffff;
            tp_dev.sta &= 0XE000;           /* 清除点有效标记 */
        }
    }

    if (t > 240)
    {
        t = 10;                             /* 重新从10开始计数 */
    }

    return res;
}
