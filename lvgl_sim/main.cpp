// ============================================================
//  Cyber Cat — LVGL Desktop Simulator
//  360×360 window, simulates ESP32-S3 round screen
//
//  Keyboard controls:
//    1  →  CAT_NEUTRAL
//    2  →  CAT_HAPPY
//    3  →  CAT_LISTENING
//    4  →  CAT_THINKING
//    5  →  CAT_SLEEPY
//    6  →  CAT_TALKING
//    SPACE  →  Simulate tap (SINGLE_CLICK)
//    H      →  Simulate long press (start LISTENING → then tap to stop)
//    ESC    →  Quit
// ============================================================
#include "lvgl.h"
#include "src/drivers/sdl/lv_sdl_window.h"
#include "src/drivers/sdl/lv_sdl_mouse.h"
#include "src/drivers/sdl/lv_sdl_mousewheel.h"
#include "src/drivers/sdl/lv_sdl_keyboard.h"
#include <SDL2/SDL.h>
#include <unistd.h>
#include <cstdio>

// Arduino.h mock must come before cat_face.h
#include "mock/Arduino.h"
#include "mock/Touch_CST816.h"
#include "cat_face.h"

// ---- Global definitions for the mocks ----
_SerialStub  Serial;
CST816_Touch touch_data;

// Pending gesture — set for one frame then auto-cleared
static GESTURE _pendingGesture = NONE;

#define DISP_W  360
#define DISP_H  360

// ---- Simple on-screen expression label ----
static lv_obj_t *g_label = nullptr;

static void updateLabel() {
    if (!g_label) return;
    char buf[64];
    snprintf(buf, sizeof(buf), "%s", catExprName(catFace.getExpression()));
    lv_label_set_text(g_label, buf);
}

// ---- SDL event handler (called every frame before lv_timer_handler) ----
static bool handleEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) return false;
        if (e.type == SDL_KEYDOWN) {
            switch (e.key.keysym.sym) {
            case SDLK_ESCAPE: return false;
            case SDLK_1: catFace.setExpression(CAT_NEUTRAL);   updateLabel(); break;
            case SDLK_2: catFace.setExpression(CAT_HAPPY);     updateLabel(); break;
            case SDLK_3: catFace.setExpression(CAT_LISTENING); updateLabel(); break;
            case SDLK_4: catFace.setExpression(CAT_THINKING);  updateLabel(); break;
            case SDLK_5: catFace.setExpression(CAT_SLEEPY);    updateLabel(); break;
            case SDLK_6: catFace.setExpression(CAT_TALKING);   updateLabel(); break;
            case SDLK_SPACE: _pendingGesture = SINGLE_CLICK;               break;
            case SDLK_h:     _pendingGesture = LONG_PRESS;                 break;
            default: break;
            }
        }
    }
    return true;
}

int main(void) {
    lv_init();

    // SDL2 display — 360×360 @ 1.5x zoom for HiDPI
    lv_display_t *disp = lv_sdl_window_create(DISP_W, DISP_H);
    lv_sdl_window_set_title(disp, "Cyber Cat Simulator");
    lv_sdl_window_set_zoom(disp, 1.5f);

    // SDL2 input (mouse only — keyboard handled manually above)
    lv_sdl_mouse_create();

    // Create cat face (same call as on ESP32)
    lv_obj_t *scr = lv_screen_active();
    catFace.create(scr);

    // Expression name label (bottom center)
    g_label = lv_label_create(scr);
    lv_label_set_text(g_label, "Neutral");
    lv_obj_set_style_text_color(g_label, lv_color_hex(0x888888), 0);
    lv_obj_align(g_label, LV_ALIGN_BOTTOM_MID, 0, -20);

    printf("=== Cyber Cat Simulator ===\n");
    printf("  1-6  : Switch expression\n");
    printf("  SPACE: Tap gesture\n");
    printf("  H    : Long-press gesture\n");
    printf("  ESC  : Quit\n");
    printf("===========================\n");

    // Main loop
    while (true) {
        if (!handleEvents()) break;

        // Inject pending gesture for one frame
        touch_data.gesture = _pendingGesture;
        _pendingGesture = NONE;

        catFace.update();
        updateLabel();

        lv_timer_handler();
        usleep(16 * 1000);   // ~60fps
    }

    return 0;
}
