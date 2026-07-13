/**
 ******************************************************************************
 * @file        usart.c
 * @version     V1.0
 * @brief       串口初始化代码
 ******************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 * 
 * 实验平台:     慧勤智远 ESP32-S3 开发板
 ******************************************************************************
 */

 #include "usart.h"


 /**
  * @brief       初始化串口
  * @param       baudrate: 波特率, 根据自己需要设置波特率值
  * @note        注意: 必须设置正确的时钟源, 否则串口波特率就会设置异常.
  * @retval      无
  */
 esp_err_t usart_init(uint32_t baudrate)
 {
	 uart_config_t uart_config = {0};                    /* 串口配置句柄 */
 
	 uart_config.baud_rate = baudrate;                   /* 波特率 */
	 uart_config.data_bits = UART_DATA_8_BITS;           /* 字长为8位数据格式 */
	 uart_config.parity = UART_PARITY_DISABLE;           /* 无奇偶校验位 */
	 uart_config.stop_bits = UART_STOP_BITS_1;           /* 一个停止位 */
	 uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;   /* 无硬件控制流 */
	 uart_config.source_clk = UART_SCLK_APB;             /* 配置时钟源 */
	 uart_config.rx_flow_ctrl_thresh = 122;              /* 硬件控制流阈值 */
	 esp_err_t ret = uart_param_config(USART_UX, &uart_config);          /* 配置uart端口 */
	 if (ret != ESP_OK)
	 {
		 return ret;
	 }
 
	 /* 配置uart引脚 */
	 ret = uart_set_pin(USART_UX, USART_TX_GPIO_PIN, USART_RX_GPIO_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
	 if (ret != ESP_OK)
	 {
		 return ret;
	 }
 
	 /* 安装串口驱动 */
	 return uart_driver_install(USART_UX, RX_BUF_SIZE * 2, RX_BUF_SIZE * 2, 20, NULL, 0);
 }
