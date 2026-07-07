/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

 #include <stdlib.h>
 #include <sys/cdefs.h>
 #include "sdkconfig.h"
 
 #if CONFIG_LCD_ENABLE_DEBUG_LOG
 // The local log level must be defined before including esp_log.h
 // Set the maximum log level for this source file
 #define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
 #endif
 
 #include "freertos/FreeRTOS.h"
 #include "freertos/task.h"
 #include "esp_lcd_panel_interface.h"
 #include "esp_lcd_panel_io.h"
 #include "esp_lcd_panel_vendor.h"
 #include "esp_lcd_panel_ops.h"
 #include "esp_lcd_panel_commands.h"
 #include "driver/gpio.h"
 #include "esp_log.h"
 #include "esp_check.h"
 #include "esp_compiler.h"
 
 #define st7796_CMD_RAMCTRL               0xb5
 #define st7796_DATA_LITTLE_ENDIAN_BIT    (1 << 3)
 
 static const char *TAG = "lcd_panel.st7796";
 
 static esp_err_t panel_st7796_del(esp_lcd_panel_t *panel);
 static esp_err_t panel_st7796_reset(esp_lcd_panel_t *panel);
 static esp_err_t panel_st7796_init(esp_lcd_panel_t *panel);
 static esp_err_t panel_st7796_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end,
										   const void *color_data);
 static esp_err_t panel_st7796_invert_color(esp_lcd_panel_t *panel, bool invert_color_data);
 static esp_err_t panel_st7796_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y);
 static esp_err_t panel_st7796_swap_xy(esp_lcd_panel_t *panel, bool swap_axes);
 static esp_err_t panel_st7796_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap);
 static esp_err_t panel_st7796_disp_on_off(esp_lcd_panel_t *panel, bool off);
 static esp_err_t panel_st7796_sleep(esp_lcd_panel_t *panel, bool sleep);
 
 typedef struct {
	 esp_lcd_panel_t base;
	 esp_lcd_panel_io_handle_t io;
	 int reset_gpio_num;
	 bool reset_level;
	 int x_gap;
	 int y_gap;
	 uint8_t fb_bits_per_pixel;
	 uint8_t madctl_val;    // save current value of LCD_CMD_MADCTL register  保存LCD_CMD_MADCTL寄存器的当前值
	 uint8_t colmod_val;    // save current value of LCD_CMD_COLMOD register  保存LCD_CMD_COLMOD寄存器的当前值
	 uint8_t ramctl_val_1;
	 uint8_t ramctl_val_2;
 } st7796_panel_t;
 
 esp_err_t
 esp_lcd_new_panel_st7796(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config,
						  esp_lcd_panel_handle_t *ret_panel)
 {
 #if CONFIG_LCD_ENABLE_DEBUG_LOG
	 esp_log_level_set(TAG, ESP_LOG_DEBUG);
 #endif
	 esp_err_t ret = ESP_OK;
	 st7796_panel_t *st7796 = NULL;
	 ESP_GOTO_ON_FALSE(io && panel_dev_config && ret_panel, ESP_ERR_INVALID_ARG, err, TAG, "invalid argument");
	 // leak detection of st7796 because saving st7796->base address  st7796的泄漏检测，因为保存st7796->基址
	 ESP_COMPILER_DIAGNOSTIC_PUSH_IGNORE("-Wanalyzer-malloc-leak")
	 st7796 = calloc(1, sizeof(st7796_panel_t));
	 ESP_GOTO_ON_FALSE(st7796, ESP_ERR_NO_MEM, err, TAG, "no mem for st7796 panel");
 
	 if (panel_dev_config->reset_gpio_num >= 0) {
		 gpio_config_t io_conf = {
			 .mode = GPIO_MODE_OUTPUT,
			 .pin_bit_mask = 1ULL << panel_dev_config->reset_gpio_num,
		 };
		 ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "configure GPIO for RST line failed");
	 }
 
	 switch (panel_dev_config->rgb_ele_order) {
	 case LCD_RGB_ELEMENT_ORDER_RGB:
		 st7796->madctl_val = 0;
		 break;
	 case LCD_RGB_ELEMENT_ORDER_BGR:
		 st7796->madctl_val |= LCD_CMD_BGR_BIT;
		 break;
	 default:
		 ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported RGB element order");
		 break;
	 }
 
	 uint8_t fb_bits_per_pixel = 0;
	 switch (panel_dev_config->bits_per_pixel) {
	 case 16: // RGB565
		 st7796->colmod_val = 0x55;
		 fb_bits_per_pixel = 16;
		 break;
	 case 18: // RGB666
		 st7796->colmod_val = 0x66;
		 // each color component (R/G/B) should occupy the 6 high bits of a byte, which means 3 full bytes are required for a pixel
		 // 每个颜色组件（R/G/B）应该占据一个字节的6个高位，这意味着一个像素需要3个完整的字节
		 fb_bits_per_pixel = 24;
		 break;
	 default:
		 ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported pixel width");
		 break;
	 }
 
	 st7796->ramctl_val_1 = 0x00;
	 st7796->ramctl_val_2 = 0xf0;    // Use big endian by default  默认使用大端序
	 if ((panel_dev_config->data_endian) == LCD_RGB_DATA_ENDIAN_LITTLE) {
		 // Use little endian  使用小尾端
		 st7796->ramctl_val_2 |= st7796_DATA_LITTLE_ENDIAN_BIT;
	 }
 
	 st7796->io = io;
	 st7796->fb_bits_per_pixel = fb_bits_per_pixel;
	 st7796->reset_gpio_num = panel_dev_config->reset_gpio_num;
	 st7796->reset_level = panel_dev_config->flags.reset_active_high;
	 st7796->base.del = panel_st7796_del;
	 st7796->base.reset = panel_st7796_reset;
	 st7796->base.init = panel_st7796_init;
	 st7796->base.draw_bitmap = panel_st7796_draw_bitmap;
	 st7796->base.invert_color = panel_st7796_invert_color;
	 st7796->base.set_gap = panel_st7796_set_gap;
	 st7796->base.mirror = panel_st7796_mirror;
	 st7796->base.swap_xy = panel_st7796_swap_xy;
	 st7796->base.disp_on_off = panel_st7796_disp_on_off;
	 st7796->base.disp_sleep = panel_st7796_sleep;
	 *ret_panel = &(st7796->base);
	 ESP_LOGD(TAG, "new st7796 panel @%p", st7796);
 
	 return ESP_OK;
 
 err:
	 if (st7796) {
		 if (panel_dev_config->reset_gpio_num >= 0) {
			 gpio_reset_pin(panel_dev_config->reset_gpio_num);
		 }
		 free(st7796);
	 }
	 return ret;
	 ESP_COMPILER_DIAGNOSTIC_POP("-Wanalyzer-malloc-leak")
 }
 
 static esp_err_t panel_st7796_del(esp_lcd_panel_t *panel)
 {
	 st7796_panel_t *st7796 = __containerof(panel, st7796_panel_t, base);
 
	 if (st7796->reset_gpio_num >= 0) {
		 gpio_reset_pin(st7796->reset_gpio_num);
	 }
	 ESP_LOGD(TAG, "del st7796 panel @%p", st7796);
	 free(st7796);
	 return ESP_OK;
 }
 
 static esp_err_t panel_st7796_reset(esp_lcd_panel_t *panel)
 {
	 st7796_panel_t *st7796 = __containerof(panel, st7796_panel_t, base);
	 esp_lcd_panel_io_handle_t io = st7796->io;
 
	 // perform hardware reset  执行硬件复位
	 if (st7796->reset_gpio_num >= 0) {
		 gpio_set_level(st7796->reset_gpio_num, st7796->reset_level);
		 vTaskDelay(pdMS_TO_TICKS(10));
		 gpio_set_level(st7796->reset_gpio_num, !st7796->reset_level);
		 vTaskDelay(pdMS_TO_TICKS(10));
	 } else { // perform software reset  执行软件复位
		 ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_SWRESET, NULL, 0), TAG,
							 "io tx param failed");
		 vTaskDelay(pdMS_TO_TICKS(20)); // spec, wait at least 5m before sending new command  Spec，至少等待5m后再发送新命令
	 }
 
	 return ESP_OK;
 }
 
 static esp_err_t panel_st7796_init(esp_lcd_panel_t *panel)
 {
	 st7796_panel_t *st7796 = __containerof(panel, st7796_panel_t, base);
	 esp_lcd_panel_io_handle_t io = st7796->io;
	 // LCD goes into sleep mode and display will be turned off after power on reset, exit sleep mode first
	 // LCD进入休眠模式，开机复位后显示屏关闭，请先退出休眠模式
	 ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_SLPOUT, NULL, 0), TAG,
						 "io tx param failed");
	 vTaskDelay(pdMS_TO_TICKS(120));
	 ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL, (uint8_t[]) {
		 st7796->madctl_val,
	 }, 1), TAG, "io tx param failed");
	 ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_COLMOD, (uint8_t[]) {
		 st7796->colmod_val,
	 }, 1), TAG, "io tx param failed");
	 ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, st7796_CMD_RAMCTRL, (uint8_t[]) {
		 st7796->ramctl_val_1, st7796->ramctl_val_2
	 }, 2), TAG, "io tx param failed");
 
	 return ESP_OK;
 }
 
 static esp_err_t panel_st7796_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end,
										   const void *color_data)
 {
	 st7796_panel_t *st7796 = __containerof(panel, st7796_panel_t, base);
	 esp_lcd_panel_io_handle_t io = st7796->io;
 
	 x_start += st7796->x_gap;
	 x_end += st7796->x_gap;
	 y_start += st7796->y_gap;
	 y_end += st7796->y_gap;
 
	 // define an area of frame memory where MCU can access  定义一个区域的帧存储器，MCU可以访问
	 ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_CASET, (uint8_t[]) {
		 (x_start >> 8) & 0xFF,
		 x_start & 0xFF,
		 ((x_end - 1) >> 8) & 0xFF,
		 (x_end - 1) & 0xFF,
	 }, 4), TAG, "io tx param failed");
	 ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_RASET, (uint8_t[]) {
		 (y_start >> 8) & 0xFF,
		 y_start & 0xFF,
		 ((y_end - 1) >> 8) & 0xFF,
		 (y_end - 1) & 0xFF,
	 }, 4), TAG, "io tx param failed");
	 // transfer frame buffer  传输帧缓冲器
	 size_t len = (x_end - x_start) * (y_end - y_start) * st7796->fb_bits_per_pixel / 8;
	 ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_color(io, LCD_CMD_RAMWR, color_data, len), TAG, "io tx color failed");
 
	 return ESP_OK;
 }
 
 static esp_err_t panel_st7796_invert_color(esp_lcd_panel_t *panel, bool invert_color_data)
 {
	 st7796_panel_t *st7796 = __containerof(panel, st7796_panel_t, base);
	 esp_lcd_panel_io_handle_t io = st7796->io;
	 int command = 0;
	 if (invert_color_data) {
		 command = LCD_CMD_INVON;
	 } else {
		 command = LCD_CMD_INVOFF;
	 }
	 ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, command, NULL, 0), TAG,
						 "io tx param failed");
	 return ESP_OK;
 }
 
 static esp_err_t panel_st7796_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y)
 {
	 st7796_panel_t *st7796 = __containerof(panel, st7796_panel_t, base);
	 esp_lcd_panel_io_handle_t io = st7796->io;
	 if (mirror_x) {
		 st7796->madctl_val |= LCD_CMD_MX_BIT;
	 } else {
		 st7796->madctl_val &= ~LCD_CMD_MX_BIT;
	 }
	 if (mirror_y) {
		 st7796->madctl_val |= LCD_CMD_MY_BIT;
	 } else {
		 st7796->madctl_val &= ~LCD_CMD_MY_BIT;
	 }
	 ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL, (uint8_t[]) {
		 st7796->madctl_val
	 }, 1), TAG, "io tx param failed");
	 return ESP_OK;
 }
 
 static esp_err_t panel_st7796_swap_xy(esp_lcd_panel_t *panel, bool swap_axes)
 {
	 st7796_panel_t *st7796 = __containerof(panel, st7796_panel_t, base);
	 esp_lcd_panel_io_handle_t io = st7796->io;
	 if (swap_axes) {
		 st7796->madctl_val |= LCD_CMD_MV_BIT;
	 } else {
		 st7796->madctl_val &= ~LCD_CMD_MV_BIT;
	 }
	 ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL, (uint8_t[]) {
		 st7796->madctl_val
	 }, 1), TAG, "io tx param failed");
	 return ESP_OK;
 }
 
 static esp_err_t panel_st7796_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap)
 {
	 st7796_panel_t *st7796 = __containerof(panel, st7796_panel_t, base);
	 st7796->x_gap = x_gap;
	 st7796->y_gap = y_gap;
	 return ESP_OK;
 }
 
 static esp_err_t panel_st7796_disp_on_off(esp_lcd_panel_t *panel, bool on_off)
 {
	 st7796_panel_t *st7796 = __containerof(panel, st7796_panel_t, base);
	 esp_lcd_panel_io_handle_t io = st7796->io;
	 int command = 0;
	 if (on_off) {
		 command = LCD_CMD_DISPON;
	 } else {
		 command = LCD_CMD_DISPOFF;
	 }
	 ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, command, NULL, 0), TAG,
						 "io tx param failed");
	 return ESP_OK;
 }
 
 static esp_err_t panel_st7796_sleep(esp_lcd_panel_t *panel, bool sleep)
 {
	 st7796_panel_t *st7796 = __containerof(panel, st7796_panel_t, base);
	 esp_lcd_panel_io_handle_t io = st7796->io;
	 int command = 0;
	 if (sleep) {
		 command = LCD_CMD_SLPIN;
	 } else {
		 command = LCD_CMD_SLPOUT;
	 }
	 ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, command, NULL, 0), TAG,
						 "io tx param failed");
	 vTaskDelay(pdMS_TO_TICKS(100));
 
	 return ESP_OK;
 }
 