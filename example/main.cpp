
/**
 * @file main
 *
 */

/*********************
 *      INCLUDES
 *********************/

#include "lvgl/lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static lv_disp_t* hal_init(int32_t w, int32_t h);
extern int app_entry(int argc, char** argv);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *      VARIABLES
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

int main(int argc, char** argv)
{
    /*Initialize LVGL*/
    lv_init();

    /*Create a default group for keyboard navigation*/
    lv_group_set_default(lv_group_create());

    /*Initialize the HAL (display, input devices, tick) for LVGL*/
    hal_init(800, 600);

    int ret = app_entry(argc, argv);
    if (ret < 0) {
        return ret;
    }

    while (1) {
        uint32_t time_till_next = lv_timer_handler();
        usleep(time_till_next * 1000);
    }

    lv_deinit();

    return 0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void indev_set_cursor(lv_indev_t* indev, lv_coord_t size = 20)
{
    lv_obj_t* cursor_obj = lv_obj_create(lv_layer_sys());
    lv_obj_remove_style_all(cursor_obj);

    lv_obj_set_size(cursor_obj, size, size);
    lv_obj_set_style_translate_x(cursor_obj, -size / 2, 0);
    lv_obj_set_style_translate_y(cursor_obj, -size / 2, 0);
    lv_obj_set_style_radius(cursor_obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(cursor_obj, LV_OPA_50, 0);
    lv_obj_set_style_bg_color(cursor_obj, lv_color_black(), 0);
    lv_obj_set_style_border_width(cursor_obj, 2, 0);
    lv_obj_set_style_border_color(cursor_obj, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_indev_set_cursor(indev, cursor_obj);
}

/**
 * Initialize the Hardware Abstraction Layer (HAL) forLVGL
 */
static lv_disp_t* hal_init(int32_t w, int32_t h)
{
    lv_disp_t* disp = lv_sdl_window_create(w, h);
    lv_indev_t* mouse = lv_sdl_mouse_create();
    lv_indev_set_group(mouse, lv_group_get_default());
    lv_indev_set_display(mouse, disp);
    indev_set_cursor(mouse);

    lv_indev_t* mousewheel = lv_sdl_mousewheel_create();
    lv_indev_set_display(mousewheel, disp);
    lv_indev_set_group(mousewheel, lv_group_get_default());

    lv_indev_t* keyboard = lv_sdl_keyboard_create();
    lv_indev_set_display(keyboard, disp);
    lv_indev_set_group(keyboard, lv_group_get_default());

    return disp;
}
