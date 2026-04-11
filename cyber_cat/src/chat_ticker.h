#pragma once
#include <lvgl.h>

// Custom Chinese font (GB2312 Level 1, 3766 chars + CJK punctuation + ASCII)
LV_FONT_DECLARE(lv_font_chinese_16);

// Create the scrolling ticker strip at the bottom of the screen.
// Must be called inside an lvgl_port_lock / lvgl_port_unlock pair.
void chatTickerCreate(lv_obj_t *parent);

// Append a user-speech line "[你] <text>" (light blue).
// Call while holding the LVGL lock.
void chatTickerAddUser(const char *text);

// Append an AI-reply line "[猫] <text>" (warm white).
// Call while holding the LVGL lock.
void chatTickerAddAI(const char *text);

// Hide the ticker box immediately (both lines).
// Call while holding the LVGL lock.
void chatTickerClear();
