#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <Arduino.h>
#include "lv_conf.h"
#include <lvgl.h>
#include <Wire.h>
#include "Display_ST77916.h"
#include "Touch_CST816.h"

#define LCD_WIDTH EXAMPLE_LCD_WIDTH
#define LCD_HEIGHT EXAMPLE_LCD_HEIGHT
#define LVGL_BUF_LEN (LCD_WIDTH * LCD_HEIGHT / 10)

#define EXAMPLE_LVGL_TICK_PERIOD_MS 10

typedef void (*lvgl_update_cb_t)(void);

void Lvgl_Init(void);
void Lvgl_Loop(void);
bool lvgl_port_lock(uint32_t timeout_ms);
void lvgl_port_unlock(void);
void lvgl_set_update_cb(lvgl_update_cb_t cb);
// Latest raw touch state from the LVGL input read (updated every indev period).
bool lvgl_touch_pressed(int16_t *x, int16_t *y);
// Diagnostics: read the touch controller every indev period instead of only on INT.
void lvgl_set_touch_always_poll(bool on);
uint32_t lvgl_get_loop_count(void);
// Completed frames pushed to the LCD (counted on the last flush of each refresh).
uint32_t lvgl_get_frame_count(void);
// Longest single lv_timer_handler() call since the previous call; resets the max.
uint32_t lvgl_take_render_max_ms(void);
