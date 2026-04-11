#include <Arduino.h>
#include <esp_task_wdt.h>

#include "I2C_Driver.h"
#include "TCA9554PWR.h"
#include "Display_ST77916.h"
#include "LVGL_Driver.h"
#include "cat_face.h"
#include "wifi_manager.h"
#include "cloud_client.h"
#include "audio_input.h"
#include "es8311_spk.h"
#include "chat_ticker.h"
#include "secrets.h"

// ============================================================
//  Constants
// ============================================================
#define IDLE_TIMEOUT_MS      120000UL   // full SLEEPY at 120s
#define IDLE_DROWSY_MS        90000UL   // startDrowsy() at 90s
#define THINKING_TIMEOUT_MS   20000UL

// ============================================================
//  Dialog state machine
// ============================================================
enum DialogState {
    DIALOG_IDLE,
    DIALOG_LISTENING,
    DIALOG_THINKING,
    DIALOG_TALKING,   // TTS playing (or timer placeholder until TTS implemented)
};

static volatile DialogState dialogState = DIALOG_IDLE;
static uint32_t _thinkingStartMs = 0;
static bool     _sttShown        = false;
static uint32_t _touchReEnableMs = 0;
static uint32_t _talkingStartMs  = 0;
static uint32_t _talkingDurMs    = 0;
static uint32_t _tickerClearMs        = 0;     // when to hide the chat ticker (0 = no pending clear)
static volatile bool _tickerClearNow  = false; // immediate clear request from Core 1

// ============================================================
//  Cross-core communication: visual updates
//  Written by main loop (Core 1), read by LVGL callback (Core 0)
// ============================================================
static volatile bool         _exprDirty   = false;
static volatile CatExpression _exprPending = CAT_NEUTRAL;

static char          _tickerUserBuf[256];
static volatile bool _tickerUserReady = false;
static char          _tickerAIBuf[1024];
static volatile bool _tickerAIReady   = false;

// ============================================================
//  Cross-core communication: gesture commands
//  Written by LVGL callbacks (Core 0), read by main loop (Core 1)
// ============================================================
static volatile bool _cmdRecord = false;
static volatile bool _cmdCancel = false;
static volatile bool _cmdThink  = false;

// ============================================================
//  Globals
// ============================================================
static lv_obj_t    *wifi_dot = nullptr;
static CloudClient  cloud;

// ============================================================
//  State helpers — called from main loop (Core 1) only
// ============================================================
static void requestExpr(CatExpression e) {
    _exprPending = e;
    _exprDirty   = true;
}

static void enterIdle() {
    dialogState = DIALOG_IDLE;
    audioInputClear();
    lvgl_set_flush_skip(false);
    _touchReEnableMs = millis() + 2000;
    _tickerClearMs   = millis() + 8000;  // hide chat ticker 8s after returning to idle
    requestExpr(CAT_NEUTRAL);
    Serial.println("[Main] -> IDLE");
}

static void enterListening() {
    dialogState = DIALOG_LISTENING;
    _tickerClearMs  = 0;            // cancel pending timed clear
    _tickerClearNow = true;         // immediately clear ticker on Core 0
    audioInputStart();
    requestExpr(CAT_LISTENING);
    Serial.println("[Main] -> LISTENING");
}

static CatExpression emotionToExpr(const char *emotion) {
    if (emotion) {
        if (strcmp(emotion, "happy")    == 0) return CAT_HAPPY;
        if (strcmp(emotion, "neutral")  == 0) return CAT_NEUTRAL;
        if (strcmp(emotion, "thinking") == 0) return CAT_THINKING;
    }
    return CAT_TALKING;  // default: "talking" or unknown
}

static void enterTalking(const char *aiText, CatExpression expr = CAT_TALKING) {
    dialogState = DIALOG_TALKING;
    _talkingStartMs = millis();
    _talkingDurMs   = 30000;  // hard watchdog — TTS should finish well before this

    // Re-enable flush so TALKING animation plays on screen
    lvgl_port_lock(0);
    lvgl_set_flush_skip(false);
    lvgl_port_unlock();

    requestExpr(expr);

    // Launch TTS: download WAV from SiliconFlow and play via ES8311 (Core 1 worker)
    if (!cloud.sendTTS(aiText)) {
        Serial.printf("[Main] sendTTS failed: %s — timer fallback\n", cloud.getError());
        cloud.consume();
        // Fallback: estimate from UTF-8 byte length (Chinese ≈ 3 bytes/char × 100ms/char)
        size_t len = aiText ? strlen(aiText) : 0;
        _talkingDurMs = (uint32_t)(len / 3) * 100;
        if (_talkingDurMs < 2000)  _talkingDurMs = 2000;
        if (_talkingDurMs > 10000) _talkingDurMs = 10000;
        Serial.printf("[Main] -> TALKING (timer fallback %ums)\n", (unsigned)_talkingDurMs);
    } else {
        Serial.println("[Main] -> TALKING (TTS started)");
    }
}

static void enterThinking() {
    audioInputStop();
    dialogState = DIALOG_THINKING;
    _thinkingStartMs = millis();
    _sttShown = false;
    requestExpr(CAT_THINKING);
    Serial.printf("[Main] -> THINKING (%u bytes recorded)\n",
                  (unsigned)audioInputGetBytes());

    // Synchronize: wait for any in-flight lv_timer_handler() to finish,
    // THEN set skip flags inside the lock so the next LVGL iteration
    // skips the renderer entirely. This prevents a race where
    // _flush_skip changes mid-render causing an infinite redraw loop.
    lvgl_port_lock(0);
    lvgl_set_flush_skip(true);
    lvgl_set_touch_skip(true);
    lvgl_port_unlock();

    if (!cloud.sendAudio(audioInputGetBuffer(), audioInputGetBytes())) {
        Serial.printf("[Main] sendAudio failed: %s\n", cloud.getError());
        cloud.consume();
        enterIdle();
    }
}

// ============================================================
//  CatFace gesture callbacks — run on Core 0 inside LVGL task.
//  MUST NOT call audio/cloud APIs. Only set command flags.
// ============================================================
static void onLongPress() {
    DialogState ds = dialogState;
    if (ds == DIALOG_LISTENING) {
        _cmdCancel = true;
    } else if (ds == DIALOG_THINKING || ds == DIALOG_TALKING) {
        Serial.println("[UI] Busy, ignoring long press");
    } else {
        _cmdRecord = true;
    }
}

static bool onTap() {
    if (dialogState == DIALOG_LISTENING) {
        _cmdThink = true;
        return true;
    }
    return false;
}

// ============================================================
//  LVGL update callback — runs on Core 0 every ~16ms,
//  INSIDE the LVGL lock, BEFORE lv_timer_handler().
//  Safe to call any LVGL / CatFace API here.
// ============================================================
static void lvglUpdateCb() {
    if (_exprDirty) {
        catFace.setExpression(_exprPending);
        _exprDirty = false;
    }

    // Clear chat ticker — immediate request from Core 1 or timed auto-clear
    if (_tickerClearNow || (_tickerClearMs != 0 && millis() >= _tickerClearMs)) {
        _tickerClearNow = false;
        _tickerClearMs  = 0;
        chatTickerClear();
    }

    if (_tickerUserReady) {
        chatTickerAddUser(_tickerUserBuf);
        _tickerUserReady = false;
    }
    if (_tickerAIReady) {
        chatTickerAddAI(_tickerAIBuf);
        _tickerAIReady = false;
    }

    // Force a full redraw whenever we return to IDLE from any active state,
    // so all pending visual changes (expression + subtitles) hit the LCD.
    static DialogState _prevDs = DIALOG_IDLE;
    DialogState ds = dialogState;
    if (_prevDs != DIALOG_IDLE && ds == DIALOG_IDLE) {
        lv_obj_invalidate(lv_screen_active());
    }
    _prevDs = ds;

    catFace.update();

    static bool lastWifi = false;
    bool connected = wifiConnected();
    if (connected != lastWifi) {
        lv_obj_set_style_bg_color(
            wifi_dot,
            connected ? lv_color_hex(0x16c79a) : lv_color_hex(0xe94560), 0);
        lastWifi = connected;
    }

    if (dialogState == DIALOG_IDLE &&
        catFace.getExpression() != CAT_SLEEPY) {
        uint32_t idleMs = millis() - catFace.lastInteractMs();
        if (idleMs >= IDLE_TIMEOUT_MS) {
            Serial.println("[UI] Idle timeout -> Sleepy");
            catFace.setExpression(CAT_SLEEPY);
        } else if (idleMs >= IDLE_DROWSY_MS) {
            catFace.startDrowsy();
        }
    }
}

// ============================================================
//  Arduino setup()
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(1000);
    randomSeed(micros());

    {
        esp_task_wdt_config_t wdtCfg = {
            .timeout_ms     = 30000,
            .idle_core_mask = 0,
            .trigger_panic  = true,
        };
        esp_task_wdt_reconfigure(&wdtCfg);
    }

    Serial.println("====================================");
    Serial.println("  Cyber Cat - Phase 4 Voice Input");
    Serial.println("====================================");

    Serial.println("[1/6] I2C & TCA9554PWR...");
    I2C_Init();
    TCA9554PWR_Init(0x00);

    Serial.println("[2/6] LCD & Touch...");
    Backlight_Init();
    LCD_Init();

    Serial.println("[3/6] LVGL...");
    Lvgl_Init();

    Serial.println("[4/6] Cat Face...");
    lvgl_port_lock(0);
    catFace.create(lv_screen_active());
    catFace.onLongPress(onLongPress);
    catFace.onTap(onTap);

    wifi_dot = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(wifi_dot);
    lv_obj_set_size(wifi_dot, 10, 10);
    lv_obj_set_style_bg_color(wifi_dot, lv_color_hex(0xe94560), 0);
    lv_obj_set_style_bg_opa(wifi_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(wifi_dot, 5, 0);
    lv_obj_remove_flag(wifi_dot,
        (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_align(wifi_dot, LV_ALIGN_TOP_MID, 0, 25);

    chatTickerCreate(lv_screen_active());
    lvgl_port_unlock();

    Serial.println("[5/6] WiFi & Cloud...");
    wifiInit(WIFI_SSID_1, WIFI_PASS_1);
    cloud.begin(LLM_API_KEY, LLM_API_BASE, LLM_MODEL,
                STT_API_KEY, STT_API_BASE,
                TTS_API_KEY, TTS_API_BASE, TTS_MODEL, TTS_VOICE);

    Serial.println("[6/6] Audio input & output (V2)...");
    audioInputInit();
    es8311Init(16000);

    // Set output volume (0–100). Adjust here to taste.
    es8311SetVolume(20);

    _tickerUserBuf[0] = '\0';
    _tickerAIBuf[0]   = '\0';

    lvgl_set_update_cb(lvglUpdateCb);

    Serial.println("====================================");
    Serial.println("  Cat ready!  (UI on Core 0, logic on Core 1)");
    Serial.println("  Long press  -> Start recording");
    Serial.println("  Tap         -> Stop recording / Happy");
    Serial.println("  Long press  -> Cancel (while recording)");
    Serial.println("  Idle 120s   -> Sleepy");
    Serial.println("====================================");
}

// ============================================================
//  Arduino loop() — Core 1, NO LVGL calls
// ============================================================
void loop() {
    wifiLoop();

    // ---- Process gesture commands from Core 0 ----
    if (_cmdRecord) {
        _cmdRecord = false;
        if (!wifiConnected()) {
            Serial.println("[Main] No WiFi — recording anyway (will fail at upload)");
        }
        enterListening();
    }
    if (_cmdCancel) {
        _cmdCancel = false;
        Serial.println("[Main] Long press cancel -> IDLE");
        audioInputStop();
        dialogState = DIALOG_IDLE;
        audioInputClear();
        requestExpr(CAT_NEUTRAL);
        Serial.println("[Main] -> IDLE");
    }
    if (_cmdThink) {
        _cmdThink = false;
        Serial.println("[Main] Tap stop recording");
        enterThinking();
    }

    // ---- Delayed touch re-enable ----
    if (_touchReEnableMs != 0 && millis() >= _touchReEnableMs) {
        _touchReEnableMs = 0;
        lvgl_set_touch_skip(false);
    }

    // ---- Audio VAD processing ----
    if (dialogState == DIALOG_LISTENING) {
        bool vadStop = audioInputProcess();
        if (vadStop) {
            enterThinking();
        }
    }

    // ---- Cloud poll ----
    if (dialogState == DIALOG_THINKING) {
        CloudState cs = cloud.poll();
        if (cs == CLOUD_STT_DONE && !_sttShown) {
            _sttShown = true;
            const char *transcript = cloud.getTranscript();
            if (transcript && transcript[0] != '\0') {
                Serial.printf("[Main] STT: %s\n", transcript);
                strlcpy(_tickerUserBuf, transcript, sizeof(_tickerUserBuf));
                _tickerUserReady = true;
            }
        } else if (cs == CLOUD_DONE) {
            const char *reply   = cloud.getResponse();
            CatExpression expr  = emotionToExpr(cloud.getEmotion());
            Serial.printf("[Main] AI: %s (emotion=%s)\n", reply, cloud.getEmotion());
            strlcpy(_tickerAIBuf, reply, sizeof(_tickerAIBuf));
            _tickerAIReady = true;
            cloud.consume();
            enterTalking(reply, expr);
        } else if (cs == CLOUD_ERROR) {
            Serial.printf("[Main] Cloud error: %s\n", cloud.getError());
            cloud.consume();
            enterIdle();
        } else if (millis() - _thinkingStartMs >= THINKING_TIMEOUT_MS) {
            Serial.println("[Main] THINKING timeout -> IDLE");
            cloud.consume();
            enterIdle();
        }
    }

    // ---- TALKING: wait for TTS worker, hard timeout as fallback ----
    if (dialogState == DIALOG_TALKING) {
        CloudState cs = cloud.poll();
        if (cs == CLOUD_TTS_DONE) {
            cloud.consume();
            enterIdle();
        } else if (cs == CLOUD_ERROR) {
            Serial.printf("[Main] TTS error: %s\n", cloud.getError());
            cloud.consume();
            enterIdle();
        } else if (millis() - _talkingStartMs >= _talkingDurMs) {
            Serial.println("[Main] TALKING timeout -> IDLE");
            cloud.consume();
            enterIdle();
        }
    }

    // ---- Periodic health report (always on, minimal overhead) ----
    static uint32_t _mainLoopCount = 0;
    _mainLoopCount++;
    static uint32_t _healthMs = 0;
    if (millis() - _healthMs >= 10000) {
        _healthMs = millis();
        Serial.printf("[Health] main=%u lvgl=%u heap=%u ds=%d wifi=%d\n",
                      (unsigned)_mainLoopCount, (unsigned)lvgl_get_loop_count(),
                      (unsigned)esp_get_free_heap_size(),
                      (int)dialogState, (int)wifiConnected());
    }

    delay(10);
}
