// ============================================================
//  Cyber Cat — LVGL Desktop Simulator
//  360×360 window, simulates ESP32-S3 round screen
//
//  Keyboard controls:
//    1-5        activity: Idle / Listening / Thinking / Talking / Sleeping
//    Q W E R T  mood: Neutral / Happy / Curious / Confused / Surprised
//    Y          pulse Surprised (0.4s)        U  pulse Confused (1.5s)
//    D          cycle decoration: none → pupils → pupils+blush → all
//    V          toggle synthetic voice level (drives Listening / Talking)
//    Z          toggle drowsiness ramp (0 → 1 over 6s, Idle only)
//    Mouse      press and hold = finger on screen
//    ESC        quit
//
//  ./lvgl_sim --shots <dir> [decor]  renders every state to <dir>/grid.ppm
//  ./lvgl_sim --decor-shots <dir>    renders decoration variants to <dir>/decor.ppm
// ============================================================
#include "lvgl.h"
#include "src/drivers/sdl/lv_sdl_window.h"
#include <SDL2/SDL.h>
#include <unistd.h>
#include <math.h>
#include <cstdio>
#include <cstring>

#include "mock/Arduino.h"
#include "mock/Touch_CST816.h"
#include "cat_face.h"

_SerialStub  Serial;
CST816_Touch touch_data;

#define DISP_W  360
#define DISP_H  360
static const float ZOOM = 1.5f;

static const uint8_t DECOR_CYCLE[] = {DECOR_NONE, DECOR_PUPILS, DECOR_PUPILS | DECOR_BLUSH, DECOR_ALL};

static lv_obj_t *g_label = nullptr;
static bool      g_voice = false;
static bool      g_drowsyRamp = false;
static uint32_t  g_drowsyStart = 0;
static int       g_decorIdx = 0;

static void updateLabel() {
    char buf[96];
    snprintf(buf, sizeof(buf), "%s / %s%s", catActivityName(catFace.activity()),
             catMoodName(catFace.mood()), g_voice ? "  [voice]" : "");
    lv_label_set_text(g_label, buf);
}

// Syllable-like level: ~4 bursts per second with random gaps.
static uint16_t syntheticVoice(uint32_t now) {
    float t = now / 1000.0f;
    float syll = 0.5f + 0.5f * sinf(t * 2 * 3.14159f * 4.0f);
    float phrase = sinf(t * 2 * 3.14159f * 0.35f) > -0.3f ? 1.0f : 0.0f;
    return (uint16_t)(syll * syll * phrase * 4500);
}

static void feedInputs() {
    uint32_t now = lv_tick_get();
    uint16_t lvl = g_voice ? syntheticVoice(now) : 0;
    catFace.setSpeechLevel(lvl);
    catFace.setListenLevel(lvl);

    if (g_drowsyRamp) catFace.setDrowsiness((now - g_drowsyStart) / 6000.0f);

    int mx, my;
    uint32_t buttons = SDL_GetMouseState(&mx, &my);
    catFace.setTouch(buttons & SDL_BUTTON(SDL_BUTTON_LEFT), (int16_t)(mx / ZOOM), (int16_t)(my / ZOOM));
}

static bool handleEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) return false;
        if (e.type != SDL_KEYDOWN) continue;
        switch (e.key.keysym.sym) {
        case SDLK_ESCAPE: return false;
        case SDLK_1: catFace.setActivity(ACT_IDLE);      break;
        case SDLK_2: catFace.setActivity(ACT_LISTENING); break;
        case SDLK_3: catFace.setActivity(ACT_THINKING);  break;
        case SDLK_4: catFace.setActivity(ACT_TALKING);   break;
        case SDLK_5: catFace.setActivity(ACT_SLEEPING);  break;
        case SDLK_q: catFace.setMood(MOOD_NEUTRAL);   break;
        case SDLK_w: catFace.setMood(MOOD_HAPPY);     break;
        case SDLK_e: catFace.setMood(MOOD_CURIOUS);   break;
        case SDLK_r: catFace.setMood(MOOD_CONFUSED);  break;
        case SDLK_t: catFace.setMood(MOOD_SURPRISED); break;
        case SDLK_y: catFace.pulseMood(MOOD_SURPRISED, 400);  break;
        case SDLK_u: catFace.pulseMood(MOOD_CONFUSED, 1500);  break;
        case SDLK_d:
            g_decorIdx = (g_decorIdx + 1) % (int)sizeof(DECOR_CYCLE);
            catFace.setDecor(DECOR_CYCLE[g_decorIdx]);
            printf("decor mask = %u\n", DECOR_CYCLE[g_decorIdx]);
            break;
        case SDLK_v: g_voice = !g_voice; break;
        case SDLK_z:
            g_drowsyRamp = !g_drowsyRamp;
            g_drowsyStart = lv_tick_get();
            if (!g_drowsyRamp) catFace.setDrowsiness(0);
            break;
        default: break;
        }
    }
    return true;
}

// ---- Snapshot tooling ----
static void runFor(uint32_t ms) {
    uint32_t end = lv_tick_get() + ms;
    while (lv_tick_get() < end) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {}
        lv_timer_handler();
        usleep(5 * 1000);
    }
}

static bool savePpm(const char *path, const uint8_t *rgb, int w, int h) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    fwrite(rgb, 1, (size_t)w * h * 3, f);
    fclose(f);
    return true;
}

// Copies the active screen (XRGB8888 snapshot) into an RGB888 canvas at (ox, oy).
static bool blitScreen(uint8_t *canvas, int canvasW, int ox, int oy) {
    lv_draw_buf_t *buf = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_XRGB8888);
    if (!buf) return false;
    for (uint32_t y = 0; y < buf->header.h; y++) {
        const uint8_t *row = buf->data + y * buf->header.stride;
        uint8_t *dst = canvas + ((size_t)(oy + y) * canvasW + ox) * 3;
        for (uint32_t x = 0; x < buf->header.w; x++) {
            dst[x * 3 + 0] = row[x * 4 + 2];
            dst[x * 3 + 1] = row[x * 4 + 1];
            dst[x * 3 + 2] = row[x * 4 + 0];
        }
    }
    lv_draw_buf_destroy(buf);
    return true;
}

struct Shot { CatActivity act; CatMood mood; uint16_t voice; };

static const Shot STATE_SHOTS[] = {
    {ACT_IDLE, MOOD_NEUTRAL, 0},   {ACT_IDLE, MOOD_HAPPY, 0},     {ACT_IDLE, MOOD_CURIOUS, 0},
    {ACT_IDLE, MOOD_CONFUSED, 0},  {ACT_IDLE, MOOD_SURPRISED, 0}, {ACT_LISTENING, MOOD_NEUTRAL, 1500},
    {ACT_THINKING, MOOD_NEUTRAL, 0}, {ACT_TALKING, MOOD_NEUTRAL, 3000}, {ACT_SLEEPING, MOOD_NEUTRAL, 0},
};
static const Shot DECOR_SHOTS[] = {
    {ACT_IDLE, MOOD_NEUTRAL, 0}, {ACT_IDLE, MOOD_HAPPY, 0}, {ACT_LISTENING, MOOD_NEUTRAL, 1500},
    {ACT_THINKING, MOOD_NEUTRAL, 0}, {ACT_TALKING, MOOD_HAPPY, 3000},
};

static void applyShot(const Shot &s) {
    catFace.setActivity(s.act);
    catFace.setMood(s.mood);
    catFace.setSpeechLevel(s.voice);
    catFace.setListenLevel(s.voice);
    updateLabel();
    runFor(1500);
    printf("  shot %s / %s\n", catActivityName(s.act), catMoodName(s.mood));
    fflush(stdout);
}

// Renders `n` shots for each decoration mask (one row per mask) into one grid.
static int renderGrid(const char *path, const Shot *shots, int n, int cols,
                      const uint8_t *decors, int rowsPerDecor) {
    const int GAP = 6;
    int rows = rowsPerDecor;
    int gw = cols * DISP_W + (cols - 1) * GAP;
    int gh = rows * DISP_H + (rows - 1) * GAP;
    uint8_t *grid = (uint8_t *)malloc((size_t)gw * gh * 3);
    memset(grid, 0x40, (size_t)gw * gh * 3);

    int cell = 0;
    for (int r = 0; r < (decors ? rowsPerDecor : 1); r++) {
        if (decors) catFace.setDecor(decors[r]);
        for (int i = 0; i < n; i++, cell++) {
            applyShot(shots[i]);
            int ox = (cell % cols) * (DISP_W + GAP);
            int oy = (cell / cols) * (DISP_H + GAP);
            if (!blitScreen(grid, gw, ox, oy)) { printf("snapshot failed\n"); return 1; }
        }
    }
    savePpm(path, grid, gw, gh);
    printf("saved %s\n", path);
    free(grid);
    return 0;
}

int main(int argc, char **argv) {
    const char *shotsDir = nullptr, *decorDir = nullptr;
    uint8_t shotDecor = DECOR_NONE;
    if (argc >= 3 && strcmp(argv[1], "--shots") == 0) {
        shotsDir = argv[2];
        if (argc >= 4) shotDecor = (uint8_t)atoi(argv[3]);
    } else if (argc >= 3 && strcmp(argv[1], "--decor-shots") == 0) {
        decorDir = argv[2];
    }

    lv_init();
    lv_display_t *disp = lv_sdl_window_create(DISP_W, DISP_H);
    lv_sdl_window_set_title(disp, "Cyber Cat Simulator");
    lv_sdl_window_set_zoom(disp, ZOOM);

    lv_obj_t *scr = lv_screen_active();
    catFace.create(scr);

    g_label = lv_label_create(scr);
    lv_obj_set_style_text_color(g_label, lv_color_hex(0x888888), 0);
    lv_obj_align(g_label, LV_ALIGN_BOTTOM_MID, 0, -20);
    updateLabel();

    char path[512];
    if (shotsDir) {
        catFace.setAutoBehaviours(false);
        catFace.setDecor(shotDecor);
        snprintf(path, sizeof(path), "%s/grid.ppm", shotsDir);
        return renderGrid(path, STATE_SHOTS, 9, 3, nullptr, 3);
    }
    if (decorDir) {
        catFace.setAutoBehaviours(false);
        snprintf(path, sizeof(path), "%s/decor.ppm", decorDir);
        return renderGrid(path, DECOR_SHOTS, 5, 5, DECOR_CYCLE, (int)sizeof(DECOR_CYCLE));
    }

    printf("=== Cyber Cat Simulator ===\n");
    printf("  1-5 activity | QWERT mood | Y/U pulse | D decor | V voice | Z drowsy | mouse touch\n");

    while (handleEvents()) {
        feedInputs();
        updateLabel();
        lv_timer_handler();
        usleep(5 * 1000);
    }
    return 0;
}
