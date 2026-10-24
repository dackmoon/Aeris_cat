#include <Arduino.h>
#include <atomic>
#include <esp_task_wdt.h>
#include <esp_heap_caps.h>

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
#include "speech_sample.h"
#include "secrets.h"

// ============================================================
//  Constants
// ============================================================
#define DROWSY_START_MS       75000UL   // eyelids start drooping
#define SLEEP_AFTER_MS       120000UL   // fall asleep
#define THINKING_TIMEOUT_MS   20000UL
#define LONG_PRESS_MS           600UL
#define DRAG_THRESHOLD_PX         20     // finger travel that turns a press into a drag
#define PET_WINDOW_MS          3500UL   // taps closer than this count as one petting streak
#define SULK_MS                1800UL
#define STRESS_GAP_MS          1500UL

// ============================================================
//  Dialog state machine (Core 1)
// ============================================================
enum DialogState {
    DIALOG_IDLE,
    DIALOG_LISTENING,
    DIALOG_THINKING,
    DIALOG_TALKING,
    DIALOG_SLEEPING,
};

static const char *dialogName(DialogState s) {
    static const char *N[] = {"Idle", "Listening", "Thinking", "Talking", "Sleeping"};
    return N[s];
}

static DialogState dialogState = DIALOG_IDLE;
static uint32_t _stateSinceMs    = 0;
static uint32_t _lastInteractMs  = 0;
static bool     _sttShown        = false;
static uint32_t _talkingDurMs    = 0;
static CatMood  _replyMood       = MOOD_NEUTRAL;

// Recorded audio waiting for the cloud worker to become free (e.g. a cancelled
// request still unwinding). Sent from loop() as soon as the worker is idle.
static const uint8_t *_sendPcm   = nullptr;
static size_t         _sendBytes = 0;

// Silent stress test ("stress <n>"): fake dialogs with the embedded sample.
static bool     _stressActive = false;
static int      _stressLeft = 0;
static int      _stressOk   = 0;
static int      _stressFail = 0;
static bool     _silent     = false;

// ============================================================
//  Cross-core: face requests (Core 1 → Core 0)
//  CatFace is only touched inside lvglUpdateCb (LVGL task).
// ============================================================
static std::atomic<int>      _reqActivity{ACT_IDLE};
static std::atomic<int>      _reqMood{MOOD_NEUTRAL};
static std::atomic<int>      _reqDrowsy{0};          // 0..1000
static std::atomic<int>      _reqDecor{DECOR_HIGHLIGHT | DECOR_BLUSH};
static std::atomic<int>      _reqSat{65};             // palette saturation, percent
static std::atomic<int>      _reqVal{85};             // palette brightness, percent
static std::atomic<int>      _reqPulseMood{MOOD_NEUTRAL};
static std::atomic<uint32_t> _reqPulseMs{0};
static std::atomic<bool>     _reqPulse{false};

static void faceActivity(CatActivity a) { _reqActivity = a; }
static void faceMood(CatMood m)         { _reqMood = m; }
static void faceDrowsy(float d)         { _reqDrowsy = (int)(d * 1000); }
static void facePulse(CatMood m, uint32_t ms) {
    _reqPulseMood = m;
    _reqPulseMs   = ms;
    _reqPulse     = true;
}

// Chat ticker (Core 1 → Core 0)
static char               _tickerUserBuf[256];
static std::atomic<bool>  _tickerUserReady{false};
static char               _tickerAIBuf[1024];
static std::atomic<bool>  _tickerAIReady{false};
static std::atomic<bool>  _tickerClearNow{false};
static uint32_t           _tickerClearMs = 0;

// Gestures (Core 0 → Core 1)
static std::atomic<bool>     _gTap{false};
static std::atomic<bool>     _gLong{false};
static std::atomic<uint32_t> _gTouchMs{0};
static std::atomic<uint32_t> _gHeldMs{0};

// ============================================================
//  Globals
// ============================================================
static lv_obj_t    *wifi_dot = nullptr;
static CloudClient  cloud;

// ============================================================
//  State helpers — Core 1 only
// ============================================================
static void setState(DialogState s) {
    if (s != dialogState) Serial.printf("[Main] %u %s -> %s\n", (unsigned)millis(), dialogName(dialogState), dialogName(s));
    dialogState   = s;
    _stateSinceMs = millis();
}

// Stops whatever the cloud worker is doing. A running worker finishes in the
// background and lands on CLOUD_IDLE; a finished result is simply dropped.
static void cloudReset() {
    if (cloud.isBusy()) cloud.cancel();
    else                cloud.consume();
}

static void stressResult(bool ok) {
    if (!_stressActive) return;
    if (ok) _stressOk++; else _stressFail++;
    Serial.printf("[Stress] %s  ok=%d fail=%d left=%d\n", ok ? "PASS" : "FAIL",
                  _stressOk, _stressFail, _stressLeft);
    if (_stressLeft == 0) {
        Serial.printf("[Stress] done: ok=%d fail=%d\n", _stressOk, _stressFail);
        _stressActive = false;
        es8311SetSilent(_silent);
    }
}

static void enterIdle() {
    setState(DIALOG_IDLE);
    audioInputClear();
    _sendPcm = nullptr;
    _lastInteractMs = millis();
    _tickerClearMs  = millis() + 8000;
    faceActivity(ACT_IDLE);
    faceDrowsy(0);
}

static void failToIdle(const char *why) {
    Serial.printf("[Main] %s -> IDLE (sad/confused)\n", why);
    cloudReset();
    enterIdle();
    faceMood(MOOD_NEUTRAL);
    bool timeout = strstr(why, "Timeout") || strstr(why, "timeout");
    facePulse(timeout ? MOOD_SAD : MOOD_CONFUSED, 2200);
    stressResult(false);
}

static void enterListening() {
    if (!wifiConnected()) {
        Serial.println("[Main] No WiFi — can't listen");
        facePulse(MOOD_SAD, 2000);
        return;
    }
    setState(DIALOG_LISTENING);
    _sendPcm        = nullptr;
    _tickerClearMs  = 0;
    _tickerClearNow = true;
    audioInputStart();
    faceMood(MOOD_NEUTRAL);
    faceActivity(ACT_LISTENING);
}

static void enterThinking(const uint8_t *pcm, size_t bytes) {
    setState(DIALOG_THINKING);
    _sttShown  = false;
    _sendPcm   = pcm;
    _sendBytes = bytes;
    faceActivity(ACT_THINKING);
    Serial.printf("[Main] %u bytes of audio queued\n", (unsigned)bytes);
}

static CatMood emotionToMood(const char *emotion) {
    if (emotion) {
        if (strcmp(emotion, "happy")     == 0) return MOOD_HAPPY;
        if (strcmp(emotion, "thinking")  == 0 || strcmp(emotion, "curious") == 0) return MOOD_CURIOUS;
        if (strcmp(emotion, "love")      == 0 || strcmp(emotion, "shy")     == 0) return MOOD_LOVE;
        if (strcmp(emotion, "sad")       == 0) return MOOD_SAD;
        if (strcmp(emotion, "angry")     == 0) return MOOD_ANGRY;
        if (strcmp(emotion, "wink")      == 0) return MOOD_WINK;
        if (strcmp(emotion, "surprised") == 0) return MOOD_SURPRISED;
        if (strcmp(emotion, "confused")  == 0) return MOOD_CONFUSED;
    }
    return MOOD_NEUTRAL;   // "neutral", "talking" or unknown
}

static void enterTalking(const char *aiText, CatMood mood) {
    setState(DIALOG_TALKING);
    _replyMood    = mood;
    _talkingDurMs = 30000;   // hard watchdog — TTS should finish well before this
    faceMood(mood);
    faceActivity(ACT_TALKING);

    if (!cloud.sendTTS(aiText)) {
        Serial.printf("[Main] sendTTS failed: %s — timer fallback\n", cloud.getError());
        cloud.consume();
        // Chinese ≈ 3 bytes/char × 100ms/char
        size_t len = aiText ? strlen(aiText) : 0;
        _talkingDurMs = (uint32_t)(len / 3) * 100;
        if (_talkingDurMs < 2000)  _talkingDurMs = 2000;
        if (_talkingDurMs > 10000) _talkingDurMs = 10000;
    }
}

static void finishTalking() {
    cloud.consume();
    enterIdle();
    faceMood(MOOD_NEUTRAL);
    if (_replyMood != MOOD_NEUTRAL) facePulse(_replyMood, 3000);   // linger a moment
    stressResult(true);
}

static void wake() {
    Serial.println("[Main] Wake up");
    enterIdle();
}

// ============================================================
//  Gestures — Core 1, dispatched by dialog state
// ============================================================
static uint32_t _lastPetMs       = 0;
static uint8_t  _petStreak       = 0;
static bool     _angry           = false;
static uint32_t _lastInterruptMs = 0;

static void onTap() {
    uint32_t now = millis();
    switch (dialogState) {
    case DIALOG_IDLE:
        if (now - _lastPetMs < PET_WINDOW_MS) _petStreak++;
        else                         _petStreak = 1;
        _lastPetMs = now;

        {
            CatMood m;
            uint32_t ms = 2200;
            if (_petStreak >= 5) {
                // Poked too many times in a row: stays angry while the poking goes on;
                // loop() turns it into a short sulk once the poking stops.
                m = MOOD_ANGRY;
                ms = PET_WINDOW_MS + 500;
                _angry = true;
            } else if (_petStreak >= 2) {
                m = MOOD_LOVE;
                ms = 2800;
            } else {
                m = (random(0, 100) < 30) ? MOOD_WINK : MOOD_HAPPY;
            }
            Serial.printf("[Touch] pet streak %u -> %s\n", (unsigned)_petStreak, catMoodName(m));
            facePulse(m, ms);
        }
        break;
    case DIALOG_SLEEPING:  wake(); break;
    case DIALOG_LISTENING:
        audioInputStop();
        enterThinking(audioInputGetBuffer(), audioInputGetBytes());
        break;
    case DIALOG_TALKING:
        Serial.println("[Main] Tap while talking -> interrupt");
        cloudReset();
        enterIdle();
        faceMood(MOOD_NEUTRAL);
        if (now - _lastInterruptMs < 15000) {
            facePulse(MOOD_ANGRY, 2000);   // interrupted again -> annoyed!
        } else {
            facePulse(MOOD_SURPRISED, 500);
        }
        _lastInterruptMs = now;
        break;
    case DIALOG_THINKING:  break;
    }
}

static void onLongPress() {
    switch (dialogState) {
    case DIALOG_IDLE:      enterListening(); break;
    case DIALOG_SLEEPING:  wake(); enterListening(); break;
    case DIALOG_LISTENING:
        Serial.println("[Main] Long press -> cancel recording");
        audioInputStop();
        enterIdle();
        break;
    case DIALOG_THINKING:
        Serial.println("[Main] Long press -> cancel request");
        cloudReset();
        enterIdle();
        break;
    case DIALOG_TALKING:
        Serial.println("[Main] Long press while talking -> interrupt and listen");
        cloudReset();
        enterListening();
        break;
    }
}

// ============================================================
//  LVGL update callback — Core 0, inside the LVGL lock, before
//  lv_timer_handler(). The only place CatFace / LVGL is touched.
// ============================================================
// Gestures are classified on release: a slow drag looks exactly like a long
// press for its first few hundred ms, so only the whole press can tell them
// apart. Drags only steer the eyes (CatFace::setTouch).
static void detectGestures(bool pressed, int16_t x, int16_t y) {
    static bool     down = false;
    static uint32_t downMs = 0, upMs = 0;
    static int16_t  downX = 0, downY = 0;
    static int32_t  maxMove = 0;
    uint32_t now = millis();
    if (pressed) {
        _gTouchMs = now;
        if (!down) {
            down = true; downMs = now;
            downX = x; downY = y; maxMove = 0;
            Serial.printf("[Touch] raw down (%d,%d) gap=%ums\n", x, y, (unsigned)(now - upMs));
        } else {
            int32_t m = abs(x - downX) + abs(y - downY);
            if (m > maxMove) maxMove = m;
        }
    } else if (down) {
        down = false;
        upMs = now;
        uint32_t held = now - downMs;
        _gHeldMs = held;
        bool drag = maxMove > DRAG_THRESHOLD_PX;
        Serial.printf("[Touch] raw up held=%ums move=%dpx%s\n", (unsigned)held, (int)maxMove,
                      drag ? " (drag)" : "");
        if (drag)                       return;
        if (held >= LONG_PRESS_MS)      _gLong = true;
        else                            _gTap = true;
    }
}

static void lvglUpdateCb() {
    catFace.setActivity((CatActivity)_reqActivity.load());
    catFace.setMood((CatMood)_reqMood.load());
    catFace.setDrowsiness(_reqDrowsy.load() / 1000.0f);
    catFace.setDecor((uint8_t)_reqDecor.load());
    catFace.setPalette(_reqSat.load() / 100.0f, _reqVal.load() / 100.0f);
    if (_reqPulse.exchange(false)) catFace.pulseMood((CatMood)_reqPulseMood.load(), _reqPulseMs.load());

    catFace.setSpeechLevel(es8311GetPlayLevel());
    catFace.setListenLevel(audioInputGetLevel());

    int16_t tx = 0, ty = 0;
    bool pressed = lvgl_touch_pressed(&tx, &ty);
    catFace.setTouch(pressed, tx, ty);
    detectGestures(pressed, tx, ty);

    if (_tickerClearNow.exchange(false) || (_tickerClearMs != 0 && millis() >= _tickerClearMs)) {
        _tickerClearMs = 0;
        chatTickerClear();
    }
    if (_tickerUserReady.load()) {
        chatTickerAddUser(_tickerUserBuf);
        _tickerUserReady = false;
    }
    if (_tickerAIReady.load()) {
        chatTickerAddAI(_tickerAIBuf);
        _tickerAIReady = false;
    }

    static bool lastWifi = false;
    bool connected = wifiConnected();
    if (connected != lastWifi) {
        lv_obj_set_style_bg_color(
            wifi_dot,
            connected ? lv_color_hex(0x16c79a) : lv_color_hex(0xe94560), 0);
        lastWifi = connected;
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
    Serial.println("  Cyber Cat");
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
    _lastInteractMs   = millis();

    lvgl_set_update_cb(lvglUpdateCb);

    Serial.println("====================================");
    Serial.println("  Cat ready!  (UI on Core 0, logic on Core 1)");
    Serial.println("  Long press  -> Listen / cancel / interrupt");
    Serial.println("  Tap         -> Send recording / interrupt / pet");
    Serial.println("  Idle 75s    -> Drowsy, 120s -> Sleep");
    Serial.println("====================================");
}

// ============================================================
//  Serial debug commands (one per line):
//    act <n>     force face activity (0 Idle .. 4 Sleeping), until next state change
//    mood <n>    set face mood (0 Neutral .. 4 Surprised)
//    pulse <n>   flash a mood for 1.5s
//    decor <m>   decoration mask: 1 pupils | 2 blush | 4 mouth (0 = off)
//    silent <b>  1 = keep the speaker amp off
//    tap / hold  simulate a tap / long press
//    chat        fake dialog with the embedded speech sample ("test-" session)
//    stress <n>  n silent fake dialogs back to back, with interrupts mixed in
// ============================================================
static void pollSerialCommands() {
    static char line[32];
    static size_t len = 0;
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c != '\n' && c != '\r') {
            if (len < sizeof(line) - 1) line[len++] = c;
            continue;
        }
        if (len == 0) continue;
        line[len] = '\0';
        len = 0;

        int n, m;
        bool idle = dialogState == DIALOG_IDLE || dialogState == DIALOG_SLEEPING;
        if (sscanf(line, "act %d", &n) == 1 && n >= 0 && n < ACT_COUNT) {
            faceActivity((CatActivity)n);
            Serial.printf("[Cmd] act -> %s\n", catActivityName((CatActivity)n));
        } else if (sscanf(line, "mood %d", &n) == 1 && n >= 0 && n < MOOD_COUNT) {
            faceMood((CatMood)n);
            Serial.printf("[Cmd] mood -> %s\n", catMoodName((CatMood)n));
        } else if (sscanf(line, "pulse %d", &n) == 1 && n >= 0 && n < MOOD_COUNT) {
            facePulse((CatMood)n, 1500);
            Serial.printf("[Cmd] pulse -> %s\n", catMoodName((CatMood)n));
        } else if (sscanf(line, "decor %d", &n) == 1 && n >= 0 && n <= DECOR_ALL) {
            _reqDecor = n;
            Serial.printf("[Cmd] decor -> %d\n", n);
        } else if (sscanf(line, "silent %d", &n) == 1) {
            _silent = n != 0;
            es8311SetSilent(_silent);
            Serial.printf("[Cmd] silent -> %d\n", (int)_silent);
        } else if (strcmp(line, "chat") == 0) {
            if (idle) {
                if (dialogState == DIALOG_SLEEPING) wake();
                enterThinking(SPEECH_PCM, SPEECH_PCM_LEN);
            } else {
                Serial.println("[Cmd] busy, chat ignored");
            }
        } else if (sscanf(line, "palette %d %d", &n, &m) == 2) {
            _reqSat = n;
            _reqVal = m;
            Serial.printf("[Cmd] palette -> saturation %d%% brightness %d%%\n", n, m);
        } else if (sscanf(line, "bl %d", &n) == 1 && n >= 0 && n <= 100) {
            Set_Backlight((uint8_t)n);
            Serial.printf("[Cmd] backlight -> %d%%\n", n);
        } else if (sscanf(line, "touchpoll %d", &n) == 1) {
            lvgl_set_touch_always_poll(n != 0);
            Serial.printf("[Cmd] touchpoll -> %d\n", n != 0);
        } else if (strcmp(line, "tap") == 0) {
            _gTap = true;
        } else if (strcmp(line, "hold") == 0) {
            _gLong = true;
        } else if (sscanf(line, "stress %d", &n) == 1 && n > 0) {
            _stressActive = true;
            _stressLeft = n;
            _stressOk = _stressFail = 0;
            es8311SetSilent(true);
            Serial.printf("[Cmd] stress x%d (silent)\n", n);
        } else {
            Serial.printf("[Cmd] unknown: %s\n", line);
        }
    }
}

// ============================================================
//  Arduino loop() — Core 1, NO LVGL calls
// ============================================================
void loop() {
    wifiLoop();
    pollSerialCommands();

    // ---- Gestures from Core 0 ----
    uint32_t touchMs = _gTouchMs.load();
    if (touchMs && (int32_t)(touchMs - _lastInteractMs) > 0) {
        _lastInteractMs = touchMs;
        if (dialogState == DIALOG_IDLE) faceDrowsy(0);
    }
    if (_gLong.exchange(false)) {
        Serial.printf("[Touch] long press in %s\n", dialogName(dialogState));
        onLongPress();
    }
    if (_gTap.exchange(false)) {
        Serial.printf("[Touch] tap (%ums) in %s\n", (unsigned)_gHeldMs.load(), dialogName(dialogState));
        onTap();
    }
    if (_angry && (dialogState != DIALOG_IDLE || millis() - _lastPetMs >= PET_WINDOW_MS)) {
        _angry = false;
        _petStreak = 0;
        if (dialogState == DIALOG_IDLE) {
            Serial.println("[Touch] poking stopped -> sulk");
            facePulse(MOOD_SAD, SULK_MS);
        }
    }

    // ---- Idle: drowsiness → sleep ----
    if (dialogState == DIALOG_IDLE) {
        uint32_t idleMs = millis() - _lastInteractMs;
        if (idleMs >= SLEEP_AFTER_MS) {
            setState(DIALOG_SLEEPING);
            faceActivity(ACT_SLEEPING);
        } else if (idleMs >= DROWSY_START_MS) {
            faceDrowsy((float)(idleMs - DROWSY_START_MS) / (SLEEP_AFTER_MS - DROWSY_START_MS));
        }
    }

    // ---- Stress test driver ----
    // Every 4th dialog is cut short while talking (tap), the next one is
    // cancelled while thinking (long press); the rest run to completion.
    static int _stressIdx = 0;
    if (_stressLeft > 0 && dialogState == DIALOG_IDLE && millis() - _stateSinceMs >= STRESS_GAP_MS) {
        _stressLeft--;
        _stressIdx++;
        enterThinking(SPEECH_PCM, SPEECH_PCM_LEN);
    }
    if (_stressActive) {
        uint32_t inState = millis() - _stateSinceMs;
        if (_stressIdx % 4 == 2 && dialogState == DIALOG_TALKING && inState >= 800) {
            Serial.println("[Stress] inject tap (interrupt talking)");
            onTap();
            stressResult(true);
        } else if (_stressIdx % 4 == 3 && dialogState == DIALOG_THINKING && inState >= 1500) {
            Serial.println("[Stress] inject long press (cancel thinking)");
            onLongPress();
            stressResult(true);
        }
    }

    // ---- Recording: VAD auto-stop ----
    if (dialogState == DIALOG_LISTENING && audioInputProcess()) {
        enterThinking(audioInputGetBuffer(), audioInputGetBytes());
    }

    // ---- Thinking: send queued audio, then poll ----
    if (dialogState == DIALOG_THINKING) {
        if (_sendPcm) {
            if (!cloud.isBusy()) {
                cloud.consume();
                cloud.setSessionPrefix(_sendPcm == SPEECH_PCM ? "test" : nullptr);
                bool ok = cloud.sendAudio(_sendPcm, _sendBytes);
                _sendPcm = nullptr;
                audioInputClear();
                if (!ok) {
                    char why[96];
                    snprintf(why, sizeof(why), "sendAudio failed: %s", cloud.getError());
                    failToIdle(why);
                }
            }
        } else {
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
                const char *reply = cloud.getResponse();
                CatMood mood = emotionToMood(cloud.getEmotion());
                Serial.printf("[Main] AI: %s (emotion=%s)\n", reply, cloud.getEmotion());
                strlcpy(_tickerAIBuf, reply, sizeof(_tickerAIBuf));
                _tickerAIReady = true;
                cloud.consume();
                enterTalking(_tickerAIBuf, mood);
            } else if (cs == CLOUD_ERROR) {
                failToIdle(cloud.getError());
            } else if (cs == CLOUD_IDLE) {
                failToIdle("request dropped");
            }
        }
        if (dialogState == DIALOG_THINKING && millis() - _stateSinceMs >= THINKING_TIMEOUT_MS) {
            failToIdle("THINKING timeout");
        }
    }

    // ---- Talking: wait for TTS worker, hard timeout as fallback ----
    if (dialogState == DIALOG_TALKING) {
        CloudState cs = cloud.poll();
        if (cs == CLOUD_TTS_DONE) {
            finishTalking();
        } else if (cs == CLOUD_ERROR) {
            failToIdle(cloud.getError());
        } else if (millis() - _stateSinceMs >= _talkingDurMs) {
            Serial.println("[Main] TALKING timeout");
            cloudReset();
            finishTalking();
        }
    }

    // ---- Periodic health report ----
    static uint32_t _mainLoopCount = 0;
    _mainLoopCount++;
    static uint32_t _healthMs = 0;
    static uint32_t _healthFrames = 0;
    uint32_t elapsed = millis() - _healthMs;
    if (elapsed >= 10000) {
        _healthMs = millis();
        uint32_t frames = lvgl_get_frame_count();
        float fps = (frames - _healthFrames) * 1000.0f / elapsed;
        _healthFrames = frames;
        Serial.printf("[Health] main=%u lvgl=%u heap=%u psram=%u ds=%s wifi=%d fps=%.1f render_max=%ums face=%s/%s\n",
                      (unsigned)_mainLoopCount, (unsigned)lvgl_get_loop_count(),
                      (unsigned)esp_get_free_heap_size(),
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                      dialogName(dialogState), (int)wifiConnected(),
                      fps, (unsigned)lvgl_take_render_max_ms(),
                      catActivityName(catFace.activity()), catMoodName(catFace.mood()));
        uint32_t us[3], calls, px;
        catFaceTakeProfile(us, &calls, &px);
        float nf = fps * elapsed / 1000.0f;
        if (nf < 1.0f) nf = 1.0f;
        Serial.printf("[Prof] per frame: ears=%.2fms eyes=%.2fms decor=%.2fms draws=%.1f px=%u\n",
                      us[0] / 1000.0f / nf, us[1] / 1000.0f / nf, us[2] / 1000.0f / nf,
                      calls / nf, (unsigned)(px / nf));
    }

    delay(10);
}
