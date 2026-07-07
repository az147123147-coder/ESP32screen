/**
 * @file lvgl.h
 * This file exists only to be compatible with Arduino's library structure
 */

#ifndef LVGL_SRC_H
#define LVGL_SRC_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "../lvgl.h"

/*********************
 *      DEFINES
 *********************/
extern uint8_t fs_open_flag;
extern uint8_t fs_read_flag;
extern uint8_t fs_write_flag;
extern uint8_t fs_dir_open_flag;
extern uint8_t fs_dir_read_flag;
/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LVGL_SRC_H*/
