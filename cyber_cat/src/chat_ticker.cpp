#include "chat_ticker.h"
#include <Arduino.h>
#include <string.h>

// Strip 4-byte UTF-8 sequences (emoji, supplementary-plane chars).
// 3-byte sequences cover all CJK Unified Ideographs (U+4E00–U+9FFF).
static void stripEmoji(const char *src, char *dst, size_t dstSize) {
    size_t di = 0;
    for (size_t si = 0; src[si] != '\0' && di + 1 < dstSize; ) {
        unsigned char c = (unsigned char)src[si];
        int seqLen = (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
        if (seqLen == 4) {
            si += 4;   // skip emoji
        } else {
            for (int k = 0; k < seqLen && src[si] != '\0' && di + 1 < dstSize; ++k)
                dst[di++] = src[si++];
        }
    }
    dst[di] = '\0';
}

// =============================================================
//  Subtitle-style display — two fixed lines at screen bottom
//  [你] last user speech   (light blue)
//  [猫] last AI reply      (warm white)
//
//  Each line wraps if long, box auto-sizes vertically.
//  Call chatTickerAddUser() / chatTickerAddAI() while holding
//  the LVGL lock.
// =============================================================

static const lv_coord_t BOX_W = 240;   // fits circular LCD chord at y≈300
static const lv_color_t COLOR_USER = lv_color_hex(0x78c8ff);  // light blue
static const lv_color_t COLOR_AI   = lv_color_hex(0xf5f0e0);  // warm white
static const lv_color_t COLOR_BG   = lv_color_hex(0x0a0a1a);  // dark navy

static lv_obj_t *s_box       = nullptr;
static lv_obj_t *s_user_lbl  = nullptr;
static lv_obj_t *s_ai_lbl    = nullptr;

// =============================================================
//  Public API
// =============================================================
void chatTickerCreate(lv_obj_t *parent) {
    // Outer container — sits at bottom centre, height auto
    s_box = lv_obj_create(parent);
    lv_obj_remove_style_all(s_box);

    lv_obj_set_width(s_box, BOX_W);
    lv_obj_set_height(s_box, LV_SIZE_CONTENT);
    lv_obj_align(s_box, LV_ALIGN_BOTTOM_MID, 0, -60);

    lv_obj_set_style_bg_color(s_box, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_box, LV_OPA_70, 0);
    lv_obj_set_style_radius(s_box, 10, 0);
    lv_obj_set_style_pad_all(s_box, 6, 0);
    lv_obj_set_style_pad_row(s_box, 3, 0);

    lv_obj_set_layout(s_box, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(s_box, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE |
                                               LV_OBJ_FLAG_SCROLLABLE));

    // User label (hidden until first speech)
    s_user_lbl = lv_label_create(s_box);
    lv_obj_set_width(s_user_lbl, BOX_W - 12);
    lv_label_set_long_mode(s_user_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_user_lbl, &lv_font_chinese_16, 0);
    lv_obj_set_style_text_color(s_user_lbl, COLOR_USER, 0);
    lv_label_set_text(s_user_lbl, "");   // start empty
    lv_obj_add_flag(s_user_lbl, LV_OBJ_FLAG_HIDDEN);

    // AI label (hidden until first reply)
    s_ai_lbl = lv_label_create(s_box);
    lv_obj_set_width(s_ai_lbl, BOX_W - 12);
    lv_label_set_long_mode(s_ai_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_ai_lbl, &lv_font_chinese_16, 0);
    lv_obj_set_style_text_color(s_ai_lbl, COLOR_AI, 0);
    lv_label_set_text(s_ai_lbl, "");
    lv_obj_add_flag(s_ai_lbl, LV_OBJ_FLAG_HIDDEN);

    // Hide the whole box until something is added
    lv_obj_add_flag(s_box, LV_OBJ_FLAG_HIDDEN);
}

void chatTickerAddUser(const char *text) {
    if (!s_user_lbl || !text || text[0] == '\0') return;

    char clean[256];
    stripEmoji(text, clean, sizeof(clean));

    char buf[256];
    snprintf(buf, sizeof(buf), "[你] %s", clean);
    lv_label_set_text(s_user_lbl, buf);
    lv_obj_remove_flag(s_user_lbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_box,      LV_OBJ_FLAG_HIDDEN);

    Serial.printf("[Ticker] %s\n", buf);
}

void chatTickerAddAI(const char *text) {
    if (!s_ai_lbl || !text || text[0] == '\0') return;

    // Hide user line — cat is now speaking
    if (s_user_lbl) lv_obj_add_flag(s_user_lbl, LV_OBJ_FLAG_HIDDEN);

    char clean[256];
    stripEmoji(text, clean, sizeof(clean));

    char buf[256];
    snprintf(buf, sizeof(buf), "[猫] %s", clean);
    lv_label_set_text(s_ai_lbl, buf);
    lv_obj_remove_flag(s_ai_lbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_box,    LV_OBJ_FLAG_HIDDEN);

    Serial.printf("[Ticker] %s\n", buf);
}
