/**
 ****************************************************************************************************
 * @file        mjpeg.c
 * @author      ALIENTEK
 * @brief       MJPEG code
 * @license     Copyright (C) 2020-2030, ALIENTEK
 ****************************************************************************************************
 * @attention
 *
 * platform     : ALIENTEK DNESP32S3 board
 * website      : www.alientek.com
 * forum        : www.openedv.com/forum.php
 *
 * change logs  :
 * version      data         notes
 * V1.0         20240430     the first version
 *
 ****************************************************************************************************
 */

 #include "mjpeg.h"
 #include "esp_heap_caps.h"
 #include "esp_jpeg_dec.h"
 #include <limits.h>
 #include <stdint.h>


 uint8_t * video_buf;
 struct jpeg_decompress_struct *cinfo;
 struct my_error_mgr *jerr;
 int Windows_Width = 0;
 int Windows_Height = 0;
 uint16_t imgoffx, imgoffy;                  /* The offset of the image in the x and y directions */
 typedef struct my_error_mgr* my_error_ptr;
 static jpeg_dec_handle_t *esp_jpeg_dec = NULL;
 static size_t video_buf_capacity = 0;
 static uint8_t *video_buf_spare = NULL;

 static uint8_t *mjpegdec_alloc_buffer(size_t buf_size)
 {
     uint8_t *buf = heap_caps_aligned_alloc(64, buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
     if (buf == NULL)
     {
         buf = heap_caps_aligned_alloc(64, buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
     }
     if (buf == NULL)
     {
         buf = heap_caps_aligned_alloc(64, buf_size, MALLOC_CAP_8BIT);
     }
     return buf;
 }
 
 /**
  * @brief       Error Exiting
  * @param       cinfo   : JPEG encoding and decoding control structure
  * @retval      None
  */
 METHODDEF(void) my_error_exit(j_common_ptr cinfo)
 {
     my_error_ptr myerr = (my_error_ptr)cinfo->err;
     (*cinfo->err->output_message) (cinfo);
     longjmp(myerr->setjmp_buffer, 1);
 }
 
 /**
  * @brief       Send a message
  * @param       cinfo       : JPEG encoding and decoding control structure
  * @param       msg_level   : Message level
  * @retval      None
  */
 METHODDEF(void) my_emit_message(j_common_ptr cinfo, int msg_level)
 {
     my_error_ptr myerr = (my_error_ptr)cinfo->err;
     if (msg_level < 0)
     {
         printf("emit msg:%d\r\n", msg_level);
         longjmp(myerr->setjmp_buffer, 1);
     }
 }
 
 /**
  * @brief       mjpegdec malloc
  * @param       None
  * @retval      None
  */
 void mjpegdec_malloc(void)
 {
     mjpegdec_video_free();
     video_buf_capacity = 0;
     if (Windows_Width <= 0 || Windows_Height <= 0 ||
         (size_t)Windows_Width > SIZE_MAX / (size_t)Windows_Height ||
         (size_t)Windows_Width * (size_t)Windows_Height > SIZE_MAX / 2)
     {
         return;
     }

     size_t raw_size = (size_t)Windows_Width * (size_t)Windows_Height * 2;
     if (raw_size > SIZE_MAX - 63)
     {
         return;
     }
     size_t buf_size = (raw_size + 63) & ~((size_t)63);
     video_buf = mjpegdec_alloc_buffer(buf_size);
     video_buf_spare = mjpegdec_alloc_buffer(buf_size);
     if (video_buf == NULL || video_buf_spare == NULL)
     {
         mjpegdec_video_free();
     }
     else
     {
         video_buf_capacity = buf_size;
     }
 }
 
 /**
  * @brief       mjpegdec video free
  * @param       None
  * @retval      None
  */
 void mjpegdec_video_free(void)
 {
     if (video_buf != NULL)
     {
         heap_caps_free(video_buf);
         video_buf = NULL;
     }
     if (video_buf_spare != NULL)
     {
         heap_caps_free(video_buf_spare);
         video_buf_spare = NULL;
     }
     video_buf_capacity = 0;
 }

 void mjpegdec_advance_buffer(void)
 {
     if (video_buf != NULL && video_buf_spare != NULL)
     {
         uint8_t *displayed_buf = video_buf;
         video_buf = video_buf_spare;
         video_buf_spare = displayed_buf;
     }
 }

 static uint8_t mjpegdec_esp_jpeg_open(void)
 {
     if (esp_jpeg_dec != NULL)
     {
         return 0;
     }

     jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
     esp_jpeg_dec = jpeg_dec_open(&config);
     if (esp_jpeg_dec == NULL)
     {
         return 1;
     }

     return 0;
 }

 static void mjpegdec_esp_jpeg_close(void)
 {
     if (esp_jpeg_dec != NULL)
     {
         jpeg_dec_close(esp_jpeg_dec);
         esp_jpeg_dec = NULL;
     }
 }

 static uint8_t mjpegdec_decode_esp_jpeg(uint8_t* buf, uint32_t bsize,mjpeg_write_cb lcd_cb)
 {
     if (buf == NULL || lcd_cb == NULL || bsize == 0 || bsize > INT_MAX || video_buf == NULL ||
         Windows_Width <= 0 || Windows_Height <= 0 ||
         (size_t)Windows_Width > SIZE_MAX / (size_t)Windows_Height ||
         (size_t)Windows_Width * (size_t)Windows_Height > SIZE_MAX / 2 ||
         (size_t)Windows_Width * (size_t)Windows_Height * 2 > video_buf_capacity) return 1;
 
     if (mjpegdec_esp_jpeg_open() != 0) return 2;
 
     jpeg_dec_io_t jpeg_io = {
         .inbuf = buf,
         .inbuf_len = bsize,
         .outbuf = video_buf,
     };
     jpeg_dec_header_info_t out_info = {0};
     jpeg_error_t ret = jpeg_dec_parse_header(esp_jpeg_dec, &jpeg_io, &out_info);
     if (ret != JPEG_ERR_OK)
     {
         mjpegdec_esp_jpeg_close();
         return 3;
     }
 
     if (out_info.width != Windows_Width || out_info.height != Windows_Height)
     {
         mjpegdec_esp_jpeg_close();
         return 4;
     }
 
     int inbuf_consumed = jpeg_io.inbuf_len - jpeg_io.inbuf_remain;
     jpeg_io.inbuf = buf + inbuf_consumed;
     jpeg_io.inbuf_len = jpeg_io.inbuf_remain;
     ret = jpeg_dec_process(esp_jpeg_dec, &jpeg_io);
     if (ret != JPEG_ERR_OK)
     {
         mjpegdec_esp_jpeg_close();
         return 5;
     }

     if (lcd_cb(Windows_Width,Windows_Height,video_buf) == 0)
     {
         mjpegdec_esp_jpeg_close();
         return 6;
     }
     mjpegdec_advance_buffer();
     return 0;
 }
 
 /**
  * @brief       Decoding JPEG images
  * @param       buf    : Jpeg data stream array
  * @param       bsize  : Array size
  * @param       lcd_cb : Drawing function pointers
  * @retval      0:succeed; !0:failed
  */
 uint8_t mjpegdec_decode(uint8_t* buf, uint32_t bsize,mjpeg_write_cb lcd_cb)
 {
     JSAMPARRAY buffer;
     if (buf == NULL || lcd_cb == NULL || cinfo == NULL || jerr == NULL || bsize == 0 || bsize > INT_MAX ||
         video_buf == NULL || Windows_Width <= 0 || Windows_Height <= 0 ||
         (size_t)Windows_Width > SIZE_MAX / (size_t)Windows_Height ||
         (size_t)Windows_Width * (size_t)Windows_Height > SIZE_MAX / 2 ||
         (size_t)Windows_Width * (size_t)Windows_Height * 2 > video_buf_capacity) return 1;
     uint8_t esp_decode_result = mjpegdec_decode_esp_jpeg(buf, bsize, lcd_cb);
     if (esp_decode_result == 0) return 0;
     if (esp_decode_result == 6) return 6;
     size_t row_stride = 0;
     size_t line_offset = 0;
     
     cinfo->err = jpeg_std_error(&jerr->pub);
     jerr->pub.error_exit = my_error_exit;
     jerr->pub.emit_message = my_emit_message;
     if (setjmp(jerr->setjmp_buffer))
     {
         jpeg_abort_decompress(cinfo);
         jpeg_destroy_decompress(cinfo);
         return 2;
     }
 
     jpeg_create_decompress(cinfo);
 
     jpeg_mem_src(cinfo, buf, bsize);
     if (jpeg_read_header(cinfo, TRUE) != JPEG_HEADER_OK)
     {
         jpeg_destroy_decompress(cinfo);
         return 3;
     }

     cinfo->out_color_space = JCS_RGB;

     jpeg_start_decompress(cinfo);

     if (cinfo->output_width != (JDIMENSION)Windows_Width ||
         cinfo->output_height != (JDIMENSION)Windows_Height ||
         cinfo->output_components != 3)
     {
         jpeg_abort_decompress(cinfo);
         jpeg_destroy_decompress(cinfo);
         return 4;
     }

     row_stride = cinfo->output_width * cinfo->output_components;
 
     buffer = (*cinfo->mem->alloc_sarray)
         ((j_common_ptr)cinfo, JPOOL_IMAGE, row_stride, 1);
     
     while (cinfo->output_scanline < cinfo->output_height)
     {
         size_t i = 0;

         if (jpeg_read_scanlines(cinfo, buffer, 1) != 1)
         {
             jpeg_abort_decompress(cinfo);
             jpeg_destroy_decompress(cinfo);
             return 5;
         }
         unsigned short tmp_color565;

         for (size_t k = 0; k < (size_t)Windows_Width * 2; k += 2)
         {
             tmp_color565 = rgb565(buffer[0][i],buffer[0][i + 1],buffer[0][i + 2]);
             video_buf[line_offset + k] = tmp_color565 & 0x00FF;
             video_buf[line_offset + k + 1] =  (tmp_color565 & 0xFF00) >> 8;

             i += 3;
         }

         line_offset += (size_t)Windows_Width * 2;
     }

     jpeg_finish_decompress(cinfo);
     jpeg_destroy_decompress(cinfo);

     if (lcd_cb(Windows_Width,Windows_Height,video_buf) == 0) return 6;
     mjpegdec_advance_buffer();

     return 0;
 }
 
 /**
  * @brief       mjpegdec init
  * @param       offx,offy:deviation
  * @retval      0:succeed; !0:failed
  */
 char mjpegdec_init(uint16_t offx, uint16_t offy)
 {
     Windows_Width = 0;
     Windows_Height = 0;
     cinfo = (struct jpeg_decompress_struct *)malloc(sizeof(struct jpeg_decompress_struct));
     jerr = (struct my_error_mgr *)malloc(sizeof(struct my_error_mgr));
 
     if (cinfo == NULL || jerr == NULL)
     {
         printf("[E][mjpeg.cpp] mjpegdec_init(): malloc failed to apply for memory\r\n");
         mjpegdec_free();
         return -1;
     }
 
     imgoffx = offx;
     imgoffy = offy;
 
     return 0;
 }
 
 /**
  * @brief       Mjpeg decoding completed, freeing memory
  * @param       None
  * @retval      None
  */
 void mjpegdec_free(void)
 {
     mjpegdec_esp_jpeg_close();
     free(cinfo);
     free(jerr);
     cinfo = NULL;
     jerr = NULL;
 }
 
