#include "cat_face.h"
#include <Arduino.h>
#include <math.h>
#include <string.h>

// ==========================================================
//  Layout — 360×360 round screen
// ==========================================================
static const float   EYE_X[2] = {110.0f, 250.0f};
static const float   EYE_Y    = 136.0f;
static const int16_t MOUTH_X  = 180;
static const int16_t MOUTH_Y  = 208;

// Background RGB888 (#0a0a1a)
static const uint8_t BG_R = 0x0a, BG_G = 0x0a, BG_B = 0x1a;
#define COL_BG lv_color_hex(0x0a0a1a)

static const float TAU = 6.2831853f;

// ==========================================================
//  Mood Poses & Luminous Palette (Cute Round Eyes, No Stripes)
// ==========================================================
typedef CatFace::Pose Pose;

// Pose:            dx   dy     w     h   corner topLid topTilt botLid   bend
static const Pose MOOD_POSE[MOOD_COUNT][2] = {
    /* NEUTRAL   */ {
        { 0,   0, 106, 106, 0.50f, 0.00f,  0.00f, 0.00f,   0.0f},
        { 0,   0, 106, 106, 0.50f, 0.00f,  0.00f, 0.00f,   0.0f},
    },
    /* HAPPY     */ {
        { 0,  -4, 114,  44, 0.50f, 0.00f,  0.00f, 0.00f,  28.0f},
        { 0,  -4, 114,  44, 0.50f, 0.00f,  0.00f, 0.00f,  28.0f},
    },
    /* CURIOUS   */ {
        {-2,  -6, 118, 118, 0.50f, 0.00f,  0.00f, 0.00f,   0.0f},
        { 2,   5,  90,  90, 0.50f, 0.00f,  0.00f, 0.00f,   0.0f},
    },
    /* CONFUSED  */ {
        {-2,   7,  88,  88, 0.50f, 0.00f,  0.00f, 0.00f,   0.0f},
        { 2,  -6, 116, 116, 0.50f, 0.00f,  0.00f, 0.00f,   0.0f},
    },
    /* SURPRISED */ {
        { 0,  -8, 124, 124, 0.50f, 0.00f,  0.00f, 0.00f,   0.0f},
        { 0,  -8, 124, 124, 0.50f, 0.00f,  0.00f, 0.00f,   0.0f},
    },
    /* LOVE      */ {
        { 0,  -2, 112, 112, 0.50f, 0.00f,  0.00f, 0.00f,   3.0f},
        { 0,  -2, 112, 112, 0.50f, 0.00f,  0.00f, 0.00f,   3.0f},
    },
    /* SAD       */ {
        {-2,   6, 108, 104, 0.50f, 0.22f, -0.46f, 0.00f,  -4.0f},
        { 2,   6, 108, 104, 0.50f, 0.22f, -0.46f, 0.00f,  -4.0f},
    },
    /* ANGRY     */ {
        { 3,   2, 110, 104, 0.50f, 0.24f,  0.50f, 0.00f,   0.0f},
        {-3,   2, 110, 104, 0.50f, 0.24f,  0.50f, 0.00f,   0.0f},
    },
    /* WINK      */ {
        { 0,  -1, 112,  38, 0.50f, 0.00f,  0.10f, 0.00f,  26.0f},
        { 0,  -3, 110, 110, 0.50f, 0.00f,  0.00f, 0.00f,   0.0f},
    },
};

static const Pose SLEEP_POSE = {0, 12, 98, 18, 0.50f, 0.0f, 0.0f, 0.0f, -16.0f};

// Smooth luminous RGB colors per mood
static const uint8_t MOOD_RGB[MOOD_COUNT][3] = {
    /* NEUTRAL   */ {0x42, 0xF5, 0xFF},  // bright ice-cyan
    /* HAPPY     */ {0x48, 0xFF, 0xE0},  // mint-cyan
    /* CURIOUS   */ {0x52, 0xF6, 0xFF},  // sky-cyan
    /* CONFUSED  */ {0x70, 0xE4, 0xFF},  // soft periwinkle-cyan
    /* SURPRISED */ {0x88, 0xFA, 0xFF},  // bright white-cyan
    /* LOVE      */ {0xFF, 0x6B, 0xB5},  // sweet pink
    /* SAD       */ {0x48, 0x9E, 0xFF},  // tear-blue
    /* ANGRY     */ {0xFF, 0x4E, 0x62},  // coral-red
    /* WINK      */ {0x4E, 0xFF, 0xE5},  // aqua-mint
};
static const uint8_t SLEEP_RGB[3] = {0x28, 0xA4, 0xC8};

// Per-parameter spring tuning:
// Size & bend springs are under-damped (0.56..0.60) so transitions have lively overshoot.
static const float P_FREQ[CatFace::P_COUNT] = {
    /* DX, DY */     5.2f, 5.2f,
    /* W,  H  */     4.8f, 4.8f,
    /* CORNER */     5.0f,
    /* TOP_LID */    6.5f,
    /* TOP_TILT */   5.5f,
    /* BOT_LID */    6.0f,
    /* BEND */       5.2f,
};
static const float P_DAMP[CatFace::P_COUNT] = {
    /* DX, DY */     0.68f, 0.68f,
    /* W,  H  */     0.56f, 0.56f,
    /* CORNER */     0.75f,
    /* TOP_LID */    0.82f,
    /* TOP_TILT */   0.75f,
    /* BOT_LID */    0.78f,
    /* BEND */       0.60f,
};
static const float RIGHT_EYE_FREQ = 1.06f;   // slight organic asymmetry between eyes

// ==========================================================
//  Helpers
// ==========================================================
CatFace catFace;

const char *catActivityName(CatActivity a) {
    static const char *n[] = {"Idle", "Listening", "Thinking", "Talking", "Sleeping"};
    static_assert(sizeof(n) / sizeof(n[0]) == ACT_COUNT, "catActivityName out of sync");
    return a < ACT_COUNT ? n[a] : "?";
}

const char *catMoodName(CatMood m) {
    static const char *n[] = {
        "Neutral", "Happy", "Curious", "Confused", "Surprised",
        "Love", "Sad", "Angry", "Wink"
    };
    static_assert(sizeof(n) / sizeof(n[0]) == MOOD_COUNT, "catMoodName out of sync");
    return m < MOOD_COUNT ? n[m] : "?";
}

static float frand(float lo, float hi) {
    return lo + (hi - lo) * (float)random(0, 10001) / 10000.0f;
}

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Smooth minimum / maximum to round off where the eyelid meets the circular eye
static inline float sminf(float a, float b, float k) {
    float h = fmaxf(k - fabsf(a - b), 0.0f) / k;
    return fminf(a, b) - h * h * k * 0.25f;
}

static inline float smaxf(float a, float b, float k) {
    float h = fmaxf(k - fabsf(a - b), 0.0f) / k;
    return fmaxf(a, b) + h * h * k * 0.25f;
}

static bool elapsed(uint32_t now, uint32_t since, uint32_t ms) {
    return (uint32_t)(now - since) < ms;
}

// Implicit Euler in <=8ms sub-steps: unconditionally stable even across hitches.
void CatFace::Spring::step(float dt) {
    float w = TAU * freq;
    int n = (int)ceilf(dt / 0.008f);
    if (n < 1) n = 1;
    float h = dt / n;
    for (int i = 0; i < n; i++) {
        v = (v + w * w * (target - x) * h) / (1.0f + 2.0f * damp * w * h + w * w * h * h);
        x += v * h;
    }
}

// ==========================================================
//  Public API
// ==========================================================
void CatFace::create(lv_obj_t *parent) {
    lv_obj_set_style_bg_color(parent, COL_BG, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    _obj = lv_obj_create(parent);
    lv_obj_remove_style_all(_obj);
    lv_obj_set_size(_obj, LV_PCT(100), LV_PCT(100));
    lv_obj_remove_flag(_obj, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_add_event_cb(_obj, _drawCb, LV_EVENT_DRAW_MAIN, this);

    for (int i = 0; i < 2; i++) {
        const float *initP = &MOOD_POSE[MOOD_NEUTRAL][i].dx;
        for (int p = 0; p < P_COUNT; p++) {
            _eye[i][p].freq = P_FREQ[p] * (i ? RIGHT_EYE_FREQ : 1.0f);
            _eye[i][p].damp = P_DAMP[p];
            _eye[i][p].snap(initP[p]);
        }
        // Boot pop-in: start closed and spring open.
        _eye[i][P_W].snap(12);
        _eye[i][P_H].snap(6);
    }

    _gazeX.freq = _gazeY.freq = 11.0f;
    _gazeX.damp = _gazeY.damp = 0.78f;

    // Secondary-motion catchlight trails the eye movement with lower frequency & under-damping.
    _hlX.freq = _hlY.freq = 6.2f;
    _hlX.damp = _hlY.damp = 0.56f;

    _colR.freq = _colG.freq = _colB.freq = 4.0f;
    _colR.damp = _colG.damp = _colB.damp = 0.90f;
    _colR.snap(MOOD_RGB[MOOD_NEUTRAL][0]);
    _colG.snap(MOOD_RGB[MOOD_NEUTRAL][1]);
    _colB.snap(MOOD_RGB[MOOD_NEUTRAL][2]);

    _blushOpa.freq = 3.0f;   _blushOpa.damp = 0.85f;
    _mouthOpen.freq = 15.0f; _mouthOpen.damp = 0.80f;

    uint32_t now = lv_tick_get();
    _lastTickMs = now;
    _activitySinceMs = now;
    _nextGazeMs = now + 1200;
    _nextClipMs = now + 3500;
    _scheduleBlink(now + 700, false, 1.0f);

    _buildRender(now, _cur);
    _timer = lv_timer_create(_timerCb, 12, this);
}

void CatFace::setActivity(CatActivity a) {
    if (a == _activity) return;
    uint32_t now = lv_tick_get();
    CatActivity prev = _activity;
    _activity = a;
    _activitySinceMs = now;
    _clip = CLIP_NONE;
    Serial.printf("[CatFace] %u activity %s -> %s\n", (unsigned)now, catActivityName(prev), catActivityName(a));

    if (prev == ACT_SLEEPING) {
        _wakeMs = now;
        _drowsy = 0;
        pulseMood(MOOD_SURPRISED, 420);
    }
    if (a == ACT_LISTENING || a == ACT_THINKING) {
        _scheduleBlink(now, true, 0.55f);
    }
    _nextGazeMs = now;
}

void CatFace::setMood(CatMood m) {
    if (m >= MOOD_COUNT) return;
    if (m != _mood) {
        Serial.printf("[CatFace] mood %s\n", catMoodName(m));
        if (_auto && _activity != ACT_SLEEPING) _scheduleBlink(lv_tick_get(), true, 0.45f);
    }
    _mood = m;
}

void CatFace::pulseMood(CatMood m, uint32_t durationMs) {
    if (m >= MOOD_COUNT) return;
    _pulseMood = m;
    _pulseUntilMs = lv_tick_get() + (durationMs ? durationMs : 1);
    if (_auto && _activity != ACT_SLEEPING) _scheduleBlink(lv_tick_get(), true, 0.45f);
}

CatMood CatFace::mood() const {
    if (_pulseUntilMs && (int32_t)(lv_tick_get() - _pulseUntilMs) < 0) return _pulseMood;
    return _mood;
}

void CatFace::setDrowsiness(float d) { _drowsy = clampf(d, 0, 1); }
void CatFace::setSpeechLevel(uint16_t rms) { _speechRms = rms; }
void CatFace::setListenLevel(uint16_t rms) { _listenRms = rms; }

void CatFace::setTouch(bool pressed, int16_t x, int16_t y) {
    if (_touch && !pressed) _nextGazeMs = lv_tick_get() + 500;
    _touch = pressed;
    _touchX = x;
    _touchY = y;
}

void CatFace::setDecor(uint8_t mask) {
    mask &= DECOR_ALL;
    if (mask == _decor) return;
    _decor = mask;
    if (_obj) lv_obj_invalidate(_obj);
}

// ==========================================================
//  Gaze, Saccadic Blinks & Idle Micro-Behaviour Theatre
// ==========================================================
void CatFace::_scheduleGaze(uint32_t now) {
    if (!_auto) {
        if (_activity == ACT_THINKING) {
            _gazeTgtX = -14.0f;
            _gazeTgtY = -12.0f;
        } else {
            _gazeTgtX = 0;
            _gazeTgtY = 0;
        }
        _nextGazeMs = now + 1000;
        return;
    }

    float prevX = _gazeTgtX, prevY = _gazeTgtY;
    float hold = 1200;

    switch (_activity) {
    case ACT_IDLE:
        if (frand(0, 1) < 0.35f) {
            _gazeTgtX = 0;
            _gazeTgtY = 0;
        } else {
            _gazeTgtX = frand(-18, 18);
            _gazeTgtY = frand(-10, 8);
        }
        hold = frand(900, 3200);
        break;
    case ACT_THINKING:
        _thinkLeft = !_thinkLeft;
        _gazeTgtX = (_thinkLeft ? -1.0f : 1.0f) * frand(10, 16);
        _gazeTgtY = -frand(10, 14);
        hold = frand(900, 1900);
        break;
    case ACT_LISTENING:
    case ACT_TALKING:
        _gazeTgtX = frand(-4, 4);
        _gazeTgtY = frand(-3, 3);
        hold = frand(600, 1400);
        break;
    default:
        _gazeTgtX = 0;
        _gazeTgtY = 0;
        hold = 1000;
        break;
    }

    // Saccadic blink: large eye shifts naturally trigger a quick partial blink.
    float jump = hypotf(_gazeTgtX - prevX, _gazeTgtY - prevY);
    if (jump > 13.0f && (int32_t)(_nextBlinkMs - now) > 250) {
        _scheduleBlink(now, true, 0.50f);
    }
    _nextGazeMs = now + (uint32_t)hold;
}

void CatFace::_scheduleBlink(uint32_t now, bool soon, float depth) {
    if (!_auto) {
        _blinkStartMs = now + 0x40000000;
        _nextBlinkMs = now + 1000;
        return;
    }
    float lo = 2200, hi = 5400;
    switch (_activity) {
    case ACT_LISTENING: lo = 3000; hi = 6200; break;
    case ACT_THINKING:  lo = 1800; hi = 4000; break;
    case ACT_TALKING:   lo = 2200; hi = 4600; break;
    default: break;
    }
    uint32_t start = now + (uint32_t)(soon ? frand(0, 60) : frand(lo, hi));

    CatMood m = mood();
    bool slow = (m == MOOD_HAPPY || m == MOOD_LOVE) && depth > 0.8f && frand(0, 1) < 0.40f;
    _blinkCloseMs  = slow ? 190 : (uint16_t)(58 * depth + 15);
    _blinkOpenMs   = slow ? 280 : (uint16_t)(105 * depth + 20);
    _blinkOffsetMs = (uint8_t)frand(0, 14);
    _blinkDepth    = depth;
    _blinkTwice    = !slow && depth > 0.8f && frand(0, 1) < 0.18f;
    _blinkStartMs  = start;

    uint32_t cycle = _blinkCloseMs + 28 + _blinkOpenMs;
    _nextBlinkMs = start + (_blinkTwice ? cycle * 2 + 55 : cycle) + _blinkOffsetMs + (soon ? (uint32_t)frand(1800, 4200) : 0);
}

float CatFace::_blinkAmount(uint32_t now, int eye) const {
    if (_activity == ACT_SLEEPING) return 0;
    int32_t t = (int32_t)(now - _blinkStartMs) - (eye ? _blinkOffsetMs : 0);
    if (t < 0) return 0;
    int32_t holdMs = 28;
    int32_t cycle = _blinkCloseMs + holdMs + _blinkOpenMs;
    if (_blinkTwice && t >= cycle + 55) t -= cycle + 55;
    if (t < _blinkCloseMs) {
        float u = (float)t / _blinkCloseMs;
        return u * u * _blinkDepth;
    }
    t -= _blinkCloseMs;
    if (t < holdMs) return _blinkDepth;
    t -= holdMs;
    if (t < _blinkOpenMs) {
        float u = 1.0f - (float)t / _blinkOpenMs;
        return u * u * _blinkDepth;
    }
    return 0;
}

// Autonomous micro-expressions during Idle so the cat feels alive even when nobody touches it.
void CatFace::_applyIdleClip(uint32_t now, Pose out[2], float &gx, float &gy) {
    if (!_auto || _activity != ACT_IDLE || mood() != MOOD_NEUTRAL || _touch) {
        _clip = CLIP_NONE;
        return;
    }

    if (_clip == CLIP_NONE && (int32_t)(now - _nextClipMs) >= 0) {
        uint32_t r = random(0, 100);
        if (_drowsy > 0.25f)      _clip = CLIP_DROWSY_NOD;
        else if (r < 30)          _clip = CLIP_LOOK_AROUND;
        else if (r < 58)          _clip = CLIP_CURIOUS_PEEK;
        else if (r < 80)          _clip = CLIP_HAPPY_SQUINT;
        else                      _clip = CLIP_ALERT_PERK;
        _clipStartMs = now;
    }

    if (_clip == CLIP_NONE) return;
    uint32_t t = now - _clipStartMs;

    switch (_clip) {
    case CLIP_LOOK_AROUND:
        if (t < 650)       { gx = -18.0f; gy = -3.0f; }
        else if (t < 1400) { gx =  18.0f; gy = -2.0f; }
        else if (t < 1900) { gx =   0.0f; gy =  0.0f; }
        else { _clip = CLIP_NONE; _nextClipMs = now + (uint32_t)frand(4000, 7500); }
        break;

    case CLIP_CURIOUS_PEEK:
        if (t < 1600) {
            out[0].w = 118; out[0].h = 118; out[0].dy -= 5;
            out[1].w =  92; out[1].h =  92; out[1].dy += 4;
            gx = 6.0f; gy = -4.0f;
        } else {
            _clip = CLIP_NONE;
            _nextClipMs = now + (uint32_t)frand(4500, 8000);
        }
        break;

    case CLIP_HAPPY_SQUINT:
        if (t < 1800) {
            for (int i = 0; i < 2; i++) {
                out[i].h = 46;
                out[i].w = 112;
                out[i].bend = 26.0f;
                out[i].dy -= 3;
            }
        } else {
            _clip = CLIP_NONE;
            _nextClipMs = now + (uint32_t)frand(5000, 9000);
        }
        break;

    case CLIP_ALERT_PERK:
        if (t < 110) {
            for (int i = 0; i < 2; i++) { out[i].h *= 0.84f; out[i].w *= 1.08f; }
        } else if (t < 1200) {
            for (int i = 0; i < 2; i++) { out[i].h += 10; out[i].w += 6; out[i].dy -= 6; }
            gx = -8.0f; gy = -9.0f;
        } else {
            _clip = CLIP_NONE;
            _nextClipMs = now + (uint32_t)frand(4500, 8500);
        }
        break;

    case CLIP_DROWSY_NOD:
        if (t < 1400) {
            float u = (float)t / 1400.0f;
            for (int i = 0; i < 2; i++) {
                out[i].topLid += 0.35f * u;
                out[i].dy += 10.0f * u;
                out[i].bend -= 8.0f * u;
            }
            gy = 8.0f * u;
        } else if (t < 1850) {
            for (int i = 0; i < 2; i++) {
                out[i].topLid = 0.0f;
                out[i].h += 8;
                out[i].dy -= 5;
            }
            gy = -5.0f;
        } else {
            _clip = CLIP_NONE;
            _nextClipMs = now + (uint32_t)frand(3500, 6000);
        }
        break;

    default:
        _clip = CLIP_NONE;
        break;
    }
}

// ==========================================================
//  Targets: mood shape → activity modifiers → interaction
// ==========================================================
void CatFace::_targets(uint32_t now, Pose out[2], uint8_t rgb[3]) const {
    CatMood m = mood();
    uint32_t inAct = now - _activitySinceMs;

    const uint8_t *pal = (_activity == ACT_SLEEPING) ? SLEEP_RGB : MOOD_RGB[m];
    rgb[0] = pal[0]; rgb[1] = pal[1]; rgb[2] = pal[2];

    for (int i = 0; i < 2; i++) {
        Pose p = MOOD_POSE[m][i];

        // Mood-specific lively continuous motion
        if (m == MOOD_LOVE) {
            float sway = sinf(TAU * (float)now / 1400.0f);
            p.dx += 5.0f * sway;
            p.dy += 2.0f * fabsf(sway);
        } else if (m == MOOD_ANGRY) {
            float tremor = sinf(TAU * (float)now / 110.0f);
            p.dx += 1.2f * tremor;
            p.topLid += 0.03f * sinf(TAU * (float)now / 900.0f);
        } else if (m == MOOD_SAD) {
            float sniff = sinf(TAU * (float)now / 2200.0f);
            p.dy += 2.0f * sniff;
        } else if (m == MOOD_CONFUSED) {
            float wob = sinf(TAU * (float)now / 1800.0f);
            p.dy += (i == 0 ? 2.5f : -2.5f) * wob;
        }

        switch (_activity) {
        case ACT_LISTENING: {
            float k = clampf(_listenEnv / 2200.0f, 0, 1);
            float s = (inAct < 85) ? 0.86f : 1.06f + 0.08f * k;
            p.h *= s;
            p.w *= (inAct < 85) ? 1.08f : (1.04f + 0.05f * k);
            p.dy -= 4.0f + 3.0f * k;
            p.topLid = 0.0f;
            break;
        }
        case ACT_THINKING: {
            // Cute round-eye pondering: both eyes stay completely round (one slightly smaller)
            // while gazing up-left / up-right in thought.
            if (i == 0) {
                p.w = 94;  p.h = 94;  p.dy += 4.0f;
            } else {
                p.w = 114; p.h = 114; p.dy -= 6.0f;
            }
            break;
        }
        case ACT_SLEEPING: {
            p = SLEEP_POSE;
            float b = sinf(TAU * (float)now / 4400.0f);
            p.bend += 2.5f * b;
            p.dy += 1.8f * b;
            p.w += 2.0f * b;
            break;
        }
        case ACT_IDLE:
            if (_drowsy > 0.01f) {
                p.topLid = clampf(p.topLid + 0.58f * _drowsy, 0, 0.85f);
                p.topTilt -= 0.22f * _drowsy;
                p.dy += 7.0f * _drowsy;
                p.h -= 14.0f * _drowsy;
                p.bend -= 8.0f * _drowsy;
            }
            break;
        default:
            break;
        }

        // Waking up: right eye lags 220ms behind left eye for an organic groggy wake
        if (_wakeMs && i == 1 && elapsed(now, _wakeMs, 220)) p = SLEEP_POSE;

        // Finger press on screen: volume-conserving squish + happy arch
        if (_touch && _activity != ACT_SLEEPING) {
            p.h *= 0.82f;
            p.w *= 1.10f;
            p.dy += 3.0f;
            p.bend += 8.0f;
        }
        out[i] = p;
    }
}

// ==========================================================
//  Frame tick
// ==========================================================
void CatFace::_timerCb(lv_timer_t *t) {
    ((CatFace *)lv_timer_get_user_data(t))->_tick();
}

static void unionArea(lv_area_t &dst, const lv_area_t &a, const lv_area_t &b) {
    dst.x1 = LV_MIN(a.x1, b.x1);
    dst.y1 = LV_MIN(a.y1, b.y1);
    dst.x2 = LV_MAX(a.x2, b.x2);
    dst.y2 = LV_MAX(a.y2, b.y2);
}

void CatFace::_tick() {
    uint32_t now = lv_tick_get();
    float dt = clampf((now - _lastTickMs) / 1000.0f, 0.001f, 0.05f);
    _lastTickMs = now;

    if (_pulseUntilMs && (int32_t)(now - _pulseUntilMs) >= 0) _pulseUntilMs = 0;
    if (_wakeMs && !elapsed(now, _wakeMs, 2000)) _wakeMs = 0;

    // Dual-envelope syllable onset detector for Talking:
    auto follow = [dt](float &env, float x, float attack, float release) {
        float tau = x > env ? attack : release;
        env += (x - env) * (1.0f - expf(-dt / tau));
    };
    float spkIn = (_activity == ACT_TALKING) ? (float)_speechRms : 0.0f;
    follow(_speechFast, spkIn, 0.015f, 0.070f);
    follow(_speechSlow, spkIn, 0.120f, 0.220f);
    follow(_listenEnv, (_activity == ACT_LISTENING) ? (float)_listenRms : 0.0f, 0.04f, 0.16f);

    if ((int32_t)(now - _nextGazeMs) >= 0) _scheduleGaze(now);
    if ((int32_t)(now - _nextBlinkMs) >= 0) _scheduleBlink(now, false, 1.0f);

    uint8_t rgb[3];
    Pose tg[2];
    _targets(now, tg, rgb);

    float clipGx = _gazeTgtX, clipGy = _gazeTgtY;
    _applyIdleClip(now, tg, clipGx, clipGy);

    bool sleeping = (_activity == ACT_SLEEPING);
    for (int i = 0; i < 2; i++) {
        const float *tv = &tg[i].dx;
        for (int p = 0; p < P_COUNT; p++) {
            Spring &s = _eye[i][p];
            float base = P_FREQ[p] * (i ? RIGHT_EYE_FREQ : 1.0f);
            s.freq = sleeping ? base * 0.45f : base;
            s.damp = sleeping ? 1.0f : P_DAMP[p];
            s.target = tv[p];
            s.step(dt);
        }
    }

    _colR.target = rgb[0]; _colG.target = rgb[1]; _colB.target = rgb[2];
    _colR.step(dt);        _colG.step(dt);        _colB.step(dt);

    if (_touch && !sleeping) {
        _gazeX.target = clampf((_touchX - 180) / 180.0f * 20.0f, -20, 20);
        _gazeY.target = clampf((_touchY - 180) / 180.0f * 13.0f, -13, 13);
    } else {
        _gazeX.target = clipGx;
        _gazeY.target = clipGy;
    }
    _gazeX.step(dt);
    _gazeY.step(dt);

    // Secondary-motion catchlight target follows gaze with extra amplitude
    _hlX.target = _gazeX.x * 1.30f;
    _hlY.target = _gazeY.x * 1.30f;
    _hlX.step(dt);
    _hlY.step(dt);

    float levelNorm = clampf(sqrtf(_speechFast / 4200.0f), 0, 1);
    CatMood m = mood();
    bool showBlush = (m == MOOD_HAPPY || m == MOOD_LOVE || m == MOOD_WINK || _touch);
    _blushOpa.target = showBlush ? (m == MOOD_LOVE ? 210.0f : 160.0f) : 0.0f;
    _mouthOpen.target = (_activity == ACT_TALKING) ? levelNorm : (m == MOOD_SURPRISED ? 0.55f : 0.0f);
    _blushOpa.step(dt);
    _mouthOpen.step(dt);

    EyeRender next[2];
    _buildRender(now, next);

    bool blushVis = (_decor & DECOR_BLUSH) && (_blushOpa.x > 2.0f);
    for (int i = 0; i < 2; i++) {
        // Include the under-eye blush in each eye's own bounding box so LVGL
        // refreshes two narrow independent boxes instead of one screen-wide bar.
        if (blushVis || _prevDecorBox.x2 > 0) {
            int16_t by2Blush = (int16_t)(EYE_Y + 74.0f + fabsf(_gazeY.x));
            if (next[i].by2 < by2Blush) next[i].by2 = by2Blush;
            if (next[i].bx1 > (int16_t)(EYE_X[i] - 48)) next[i].bx1 = (int16_t)(EYE_X[i] - 48);
            if (next[i].bx2 < (int16_t)(EYE_X[i] + 48)) next[i].bx2 = (int16_t)(EYE_X[i] + 48);
        }
        lv_area_t curBox = {next[i].bx1, next[i].by1, next[i].bx2, next[i].by2};
        if (_hasPrevBox) {
            lv_area_t dirty;
            unionArea(dirty, _prevEyeBox[i], curBox);
            lv_obj_invalidate_area(_obj, &dirty);
        } else {
            lv_obj_invalidate_area(_obj, &curBox);
        }
        _prevEyeBox[i] = curBox;
        _cur[i] = next[i];
    }

    bool mouthVis = (_decor & DECOR_MOUTH) != 0;
    if (mouthVis || _prevDecorBox.x1 > 0) {
        lv_area_t mbox = {MOUTH_X - 28, MOUTH_Y - 12, MOUTH_X + 28, MOUTH_Y + 22};
        lv_obj_invalidate_area(_obj, &mbox);
        _prevDecorBox.x1 = mouthVis ? 1 : 0;
    }
    _prevDecorBox.x2 = blushVis ? 1 : 0;
    _hasPrevBox = true;
}

void CatFace::_buildRender(uint32_t now, EyeRender out[2]) {
    float gx = _gazeX.x, gy = _gazeY.x;
    float velSkewX = clampf(_gazeX.v * 0.00045f, -0.20f, 0.20f);

    float speechLvl  = (_activity == ACT_TALKING) ? clampf(sqrtf(_speechFast / 4200.0f), 0, 1) : 0.0f;
    float speechBeat = (_activity == ACT_TALKING) ? clampf((_speechFast - _speechSlow) / 1800.0f, -0.3f, 1.0f) : 0.0f;

    bool breathing = (_activity == ACT_IDLE || _activity == ACT_LISTENING || _activity == ACT_THINKING);
    float breath = breathing ? sinf(TAU * (float)now / 3600.0f) : 0.0f;
    CatMood m = mood();

    uint8_t cr = (uint8_t)clampf(_colR.x, 0, 255);
    uint8_t cg = (uint8_t)clampf(_colG.x, 0, 255);
    uint8_t cb = (uint8_t)clampf(_colB.x, 0, 255);

    for (int i = 0; i < 2; i++) {
        const Spring *s = _eye[i];
        float dx      = s[P_DX].x + gx;
        float dy      = s[P_DY].x + gy;
        float w       = s[P_W].x;
        float h       = s[P_H].x;
        float corner  = s[P_CORNER].x;
        float topLid  = s[P_TOP_LID].x;
        float topTilt = s[P_TOP_TILT].x;
        float botLid  = s[P_BOT_LID].x;
        float bend    = s[P_BEND].x;

        // Breathing gently expands height and lifts the eye
        h  *= 1.0f + 0.025f * breath;
        dy -= 1.4f * breath;

        // Perspective: when looking sideways, the nearer eye grows & the farther eye shrinks slightly
        float side = (i == 0) ? -1.0f : 1.0f;
        float persp = 1.0f + 0.0042f * gx * side;
        w *= persp;
        h *= persp;

        // Talking: syllable onset beat bounces the eye & arches the lower lid (volume-conserving!)
        if (_activity == ACT_TALKING) {
            float bounce = 0.14f * speechLvl + 0.22f * speechBeat;
            h    *= 1.0f - 0.24f * bounce;
            w    *= 1.0f + 0.12f * bounce;
            dy   -= 5.5f * bounce;
            bend += 14.0f * bounce;
        }

        // Blink: volume-conserving vertical squash + downward lid sweep
        float b = _blinkAmount(now, i);
        if (b > 0.001f) {
            float h0 = h;
            h   += (8.0f - h) * b;
            w   *= 1.0f + 0.14f * b;
            dy  += (h0 - h) * 0.18f;
            bend = bend * (1.0f - b) - 10.0f * b;
        }

        if (w < 12.0f) w = 12.0f;
        if (h < 6.0f)  h = 6.0f;

        EyeRender &e = out[i];
        e.cx      = EYE_X[i] + dx;
        e.cy      = EYE_Y + dy;
        e.hw      = w * 0.5f;
        e.hh      = h * 0.5f;
        e.corner  = clampf(corner, 0.35f, 0.50f);
        e.topLid  = clampf(topLid, 0.0f, 0.92f);
        e.topTilt = clampf(topTilt, -0.65f, 0.65f);
        e.botLid  = clampf(botLid, 0.0f, 0.85f);
        e.bend    = bend;
        e.skewX   = velSkewX;
        e.skewY   = topTilt * 4.5f;

        // Primary catchlight sits in upper-left of the round eye
        e.hlX = e.cx - e.hw * 0.30f + (_hlX.x - gx) * 0.85f;
        e.hlY = e.cy - e.hh * 0.28f + (_hlY.x - gy) * 0.85f - e.bend * 0.40f;

        e.r = cr; e.g = cg; e.b = cb;
        e.tear  = (m == MOOD_SAD && _activity != ACT_SLEEPING);
        e.heart = (m == MOOD_LOVE && _activity != ACT_SLEEPING);

        float maxBend = fabsf(e.bend) + fabsf(e.skewY) + 6.0f;
        float maxSkew = fabsf(e.skewX) * e.hh + 6.0f;
        e.bx1 = (int16_t)floorf(e.cx - e.hw - maxSkew);
        e.bx2 = (int16_t)ceilf(e.cx + e.hw + maxSkew);
        e.by1 = (int16_t)floorf(e.cy - e.hh - maxBend);
        e.by2 = (int16_t)ceilf(e.cy + e.hh + maxBend);
    }
}

// ==========================================================
//  Smooth Round-Eye Scanline SDF Rasterizer (Stripe-Free!)
// ==========================================================
static inline uint16_t packRgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static inline void putPixel(lv_layer_t *layer, int32_t x, int32_t y, uint8_t r, uint8_t g, uint8_t b) {
    lv_draw_buf_t *db = layer->draw_buf;
    int32_t bx = x - layer->buf_area.x1;
    int32_t by = y - layer->buf_area.y1;
    uint8_t *row = db->data + by * db->header.stride;
    if (layer->color_format == LV_COLOR_FORMAT_RGB565) {
        ((uint16_t *)row)[bx] = packRgb565(r, g, b);
    } else if (layer->color_format == LV_COLOR_FORMAT_XRGB8888 ||
               layer->color_format == LV_COLOR_FORMAT_ARGB8888) {
        uint8_t *p = row + bx * 4;
        p[0] = b; p[1] = g; p[2] = r; p[3] = 0xFF;
    }
}

// Signed distance to a plump 2D heart centered at (0, 0) with half-size `s`.
static inline bool insideHeart(float dx, float dy, float s) {
    float x = fabsf(dx) / s;
    float y = -dy / s + 0.25f; // +y is up
    if (y > 0.85f || y < -0.95f || x > 0.95f) return false;
    if (y >= 0.05f) {
        float cx = x - 0.42f, cy = y - 0.30f;
        return (cx * cx + cy * cy) <= 0.46f * 0.46f;
    } else {
        return x <= 0.88f * (y + 0.90f);
    }
}

void CatFace::_rasterizeEye(lv_layer_t *layer, int eyeIdx, const EyeRender &e) {
    if (!layer || !layer->draw_buf) return;

    lv_area_t clip;
    clip.x1 = LV_MAX(layer->_clip_area.x1, (int32_t)e.bx1);
    clip.y1 = LV_MAX(layer->_clip_area.y1, (int32_t)e.by1);
    clip.x2 = LV_MIN(layer->_clip_area.x2, (int32_t)e.bx2);
    clip.y2 = LV_MIN(layer->_clip_area.y2, (int32_t)e.by2);
    if (clip.x1 > clip.x2 || clip.y1 > clip.y2) return;

    const float hw = e.hw, hh = e.hh;
    const float minDim = fminf(hw, hh);
    const bool  showHl = (catFace._decor & DECOR_HIGHLIGHT) && (hh > 22.0f);
    const bool  is565  = (layer->color_format == LV_COLOR_FORMAT_RGB565);

    // Precompute 16 anti-aliased coverage levels — 100% solid smooth fill (NO dark stripes!)
    uint16_t lut565[16];
    uint8_t  lutRgb[16][3];
    for (int c = 0; c < 16; c++) {
        float cov = (float)c / 15.0f;
        uint8_t r = (uint8_t)(BG_R + (e.r - BG_R) * cov);
        uint8_t g = (uint8_t)(BG_G + (e.g - BG_G) * cov);
        uint8_t b = (uint8_t)(BG_B + (e.b - BG_B) * cov);
        lut565[c]    = packRgb565(r, g, b);
        lutRgb[c][0] = r;
        lutRgb[c][1] = g;
        lutRgb[c][2] = b;
    }
    const uint16_t solid565 = lut565[15];
    const uint16_t heart565 = packRgb565(255, 232, 245);
    const uint16_t white565 = packRgb565(255, 255, 255);

    // Dual cute round catchlights: a big one in upper-left + a small one in lower-right
    const float hl1X = e.hlX,                  hl1Y = e.hlY,                  hl1R = 11.0f;
    const float hl2X = e.cx + e.hw * 0.30f + (e.hlX - (e.cx - e.hw * 0.30f)) * 0.7f;
    const float hl2Y = e.cy + e.hh * 0.28f + (e.hlY - (e.cy - e.hh * 0.28f)) * 0.7f;
    const float hl2R = 5.0f;

    lv_draw_buf_t *db = layer->draw_buf;
    const int32_t bufX1   = layer->buf_area.x1;
    const int32_t bufY1   = layer->buf_area.y1;
    const uint32_t stride = db->header.stride;
    const float heartScale = minDim * 0.58f;
    const uint32_t tickNow = lv_tick_get();

    const int32_t xStart = clip.x1;
    const int32_t xEnd   = clip.x2;

    for (int32_t x = xStart; x <= xEnd; x++) {
        float dx = (float)x - e.cx;
        float ax = fabsf(dx);
        float dxEdge = hw - ax;
        if (dxEdge < -1.4f) continue;

        float u = clampf(dx / hw, -1.0f, 1.0f);
        float u2 = u * u;
        float uIn = (eyeIdx == 0) ? u : -u; // +1 = inner corner, -1 = outer corner

        // 1. True elliptical / circular contour: dyCircle = hh * sqrt(1 - u^2)
        float circleTerm = fmaxf(0.0f, 1.0f - u2);
        float sqrtCircle = sqrtf(circleTerm);
        float dyCircle   = hh * sqrtCircle;

        // 2. Smoothly rounded upper & lower eyelids (smax/smin rounds off sharp corners!)
        float yRelTop = -dyCircle;
        if (e.topLid > 0.005f || fabsf(e.topTilt) > 0.005f) {
            float lidTop = -hh + e.topLid * 2.0f * hh + uIn * e.topTilt * hh * 1.10f;
            yRelTop = smaxf(-dyCircle, lidTop, 10.0f);
        }

        float yRelBot = dyCircle;
        if (e.botLid > 0.005f) {
            float lidBot = hh - e.botLid * 2.0f * hh * (1.0f - 0.20f * u2);
            yRelBot = sminf(dyCircle, lidBot, 10.0f);
        }

        if (yRelBot - yRelTop < -1.0f) continue;

        // 3. Parabolic centerline bend (creates plump round-tipped ∩ happy & ︶ sleep arches)
        float yMid = e.cy + e.skewY * uIn - e.bend * (1.0f - u2);
        float yTop = yMid + yRelTop;
        float yBot = yMid + yRelBot;

        // Contour slope |dy/dx| for uniform anti-aliasing all around the circle
        float circleSlope = (hh / hw) * (fabsf(u) / fmaxf(0.18f, sqrtCircle));
        float bendSlope   = fabsf(2.0f * e.bend * u) / hw;
        float slope       = circleSlope + bendSlope;
        float invSlopeLen = 1.0f / sqrtf(1.0f + slope * slope);

        int32_t y1 = LV_MAX(clip.y1, (int32_t)floorf(yTop - 1.5f));
        int32_t y2 = LV_MIN(clip.y2, (int32_t)ceilf(yBot + 1.5f));
        if (y1 > y2) continue;

        const int32_t bx = x - bufX1;
        float aaBand = 1.35f / invSlopeLen;
        int32_t yInt1 = (dxEdge >= 1.3f) ? LV_MAX(y1, (int32_t)ceilf(yTop + aaBand)) : (y2 + 1);
        int32_t yInt2 = (dxEdge >= 1.3f) ? LV_MIN(y2, (int32_t)floorf(yBot - aaBand)) : (y1 - 1);

        auto drawEdgeRange = [&](int32_t ya, int32_t yb) {
            for (int32_t y = ya; y <= yb; y++) {
                float fy = (float)y;
                float dVert = fminf(fy - yTop, yBot - fy) * invSlopeLen;
                float d = fminf(dVert, dxEdge);
                if (d < -1.4f) continue;
                int covIdx;
                if (d < 0.0f) {
                    float g = (d + 1.4f) * (1.0f / 1.4f);
                    covIdx = (int)(3.3f * g * g);
                } else if (d < 1.3f) {
                    covIdx = 3 + (int)(12.0f * (d * (1.0f / 1.3f)));
                } else {
                    covIdx = 15;
                }
                if (covIdx > 15) covIdx = 15;
                if (is565) {
                    uint8_t *row = db->data + (y - bufY1) * stride;
                    ((uint16_t *)row)[bx] = lut565[covIdx];
                } else {
                    putPixel(layer, x, y, lutRgb[covIdx][0], lutRgb[covIdx][1], lutRgb[covIdx][2]);
                }
            }
        };

        if (yInt1 > yInt2) {
            drawEdgeRange(y1, y2);
        } else {
            drawEdgeRange(y1, yInt1 - 1);

            // Solid interior span — pure smooth color, zero dark stripes, ultra fast!
            if (is565) {
                uint8_t *p = db->data + (yInt1 - bufY1) * stride + bx * 2;
                for (int32_t y = yInt1; y <= yInt2; y++) {
                    *(uint16_t *)p = solid565;
                    p += stride;
                }
            } else {
                for (int32_t y = yInt1; y <= yInt2; y++) {
                    putPixel(layer, x, y, lutRgb[15][0], lutRgb[15][1], lutRgb[15][2]);
                }
            }

            drawEdgeRange(yInt2 + 1, y2);

            // Inner overlays: glowing heart or dual cute round catchlights
            if (e.heart && fabsf(dx) < heartScale) {
                float yc = (yTop + yBot) * 0.5f;
                int32_t hy1 = LV_MAX(yInt1, (int32_t)(yc - heartScale));
                int32_t hy2 = LV_MIN(yInt2, (int32_t)(yc + heartScale));
                for (int32_t y = hy1; y <= hy2; y++) {
                    if (insideHeart(dx, (float)y - yc, heartScale)) {
                        if (is565) {
                            uint8_t *row = db->data + (y - bufY1) * stride;
                            ((uint16_t *)row)[bx] = heart565;
                        } else {
                            putPixel(layer, x, y, 255, 232, 245);
                        }
                    }
                }
            } else if (showHl) {
                // Helper for a crisp, round anti-aliased white catchlight
                auto drawCatchlight = [&](float cxHl, float cyHl, float rHl) {
                    float hdx = (float)x - cxHl;
                    if (fabsf(hdx) > rHl) return;
                    float dyMax = sqrtf(rHl * rHl - hdx * hdx);
                    int32_t hy1 = LV_MAX(yInt1, (int32_t)ceilf(cyHl - dyMax));
                    int32_t hy2 = LV_MIN(yInt2, (int32_t)floorf(cyHl + dyMax));
                    for (int32_t y = hy1; y <= hy2; y++) {
                        if (is565) {
                            uint8_t *row = db->data + (y - bufY1) * stride;
                            ((uint16_t *)row)[bx] = white565;
                        } else {
                            putPixel(layer, x, y, 255, 255, 255);
                        }
                    }
                };
                drawCatchlight(hl1X, hl1Y, hl1R);
                drawCatchlight(hl2X, hl2Y, hl2R);
            }

            // Watery tear-drop catchlight at the bottom of the eye in MOOD_SAD (no stripes!)
            if (e.tear) {
                float tearX = e.cx + (eyeIdx == 0 ? -12.0f : 12.0f);
                float tearY = e.cy + e.hh * 0.55f + 2.0f * sinf((float)tickNow * 0.008f);
                float tdx = (float)x - tearX;
                if (fabsf(tdx) <= 6.5f) {
                    float tdyMax = sqrtf(fmaxf(0.0f, 6.5f * 6.5f - tdx * tdx));
                    int32_t ty1 = LV_MAX(yInt1, (int32_t)ceilf(tearY - tdyMax));
                    int32_t ty2 = LV_MIN(yInt2, (int32_t)floorf(tearY + tdyMax));
                    for (int32_t y = ty1; y <= ty2; y++) {
                        if (is565) {
                            uint8_t *row = db->data + (y - bufY1) * stride;
                            ((uint16_t *)row)[bx] = white565;
                        } else {
                            putPixel(layer, x, y, 255, 255, 255);
                        }
                    }
                }
            }
        }
    }
}

// Smooth rosy oval blush (no stripes!) & cute cat mouth
void CatFace::_rasterizeDecor(lv_layer_t *layer, uint32_t now) const {
    if (!layer || !layer->draw_buf) return;
    (void)now;

    float avgDx = ((_cur[0].cx - EYE_X[0]) + (_cur[1].cx - EYE_X[1])) * 0.4f;
    float avgDy = ((_cur[0].cy - EYE_Y)    + (_cur[1].cy - EYE_Y))    * 0.4f;

    // 1. Soft, seamless rosy oval blush under each eye (zero stripes!)
    float bOpa = clampf(_blushOpa.x / 255.0f, 0.0f, 1.0f);
    if ((_decor & DECOR_BLUSH) && bOpa > 0.02f) {
        for (int i = 0; i < 2; i++) {
            float side = (i == 0) ? -1.0f : 1.0f;
            int32_t bcx = (int32_t)(EYE_X[i] + side * 14.0f + avgDx);
            int32_t bcy = (int32_t)(EYE_Y + 60.0f + avgDy);

            int32_t x1 = LV_MAX(layer->_clip_area.x1, bcx - 24);
            int32_t x2 = LV_MIN(layer->_clip_area.x2, bcx + 24);
            int32_t y1 = LV_MAX(layer->_clip_area.y1, bcy - 10);
            int32_t y2 = LV_MIN(layer->_clip_area.y2, bcy + 10);

            const float scale = bOpa * 0.65f * (1.0f / (576.0f * 576.0f));
            for (int32_t y = y1; y <= y2; y++) {
                int32_t dy = y - bcy;
                int32_t dyNorm = dy * dy * 6; // 10^2 * 6 ~= 576
                for (int32_t x = x1; x <= x2; x++) {
                    int32_t dx = x - bcx;
                    int32_t env = 576 - (dx * dx + dyNorm);
                    if (env <= 0) continue;
                    // Smooth quadratic falloff so the blush is a soft glowing cloud
                    float a = (float)(env * env) * scale;
                    uint8_t r = (uint8_t)(BG_R + (0xFF - BG_R) * a);
                    uint8_t g = (uint8_t)(BG_G + (0x64 - BG_G) * a);
                    uint8_t b = (uint8_t)(BG_B + (0xA6 - BG_B) * a);
                    putPixel(layer, x, y, r, g, b);
                }
            }
        }
    }

    // 2. Small cute cat mouth "ω" (only when DECOR_MOUTH is enabled)
    if (_decor & DECOR_MOUTH) {
        float mx = MOUTH_X + avgDx;
        float my = MOUTH_Y + avgDy;
        float open = clampf(_mouthOpen.x, 0.0f, 1.0f) * 10.0f;
        uint8_t mr = _cur[0].r, mg = _cur[0].g, mb = _cur[0].b;

        int32_t x1 = LV_MAX(layer->_clip_area.x1, (int32_t)(mx - 18));
        int32_t x2 = LV_MIN(layer->_clip_area.x2, (int32_t)(mx + 18));
        int32_t y1 = LV_MAX(layer->_clip_area.y1, (int32_t)(my - 6));
        int32_t y2 = LV_MIN(layer->_clip_area.y2, (int32_t)(my + 16));

        for (int32_t x = x1; x <= x2; x++) {
            float dx = x - mx;
            if (fabsf(dx) > 15.0f) continue;
            float lobe = (fabsf(dx) - 7.5f) / 7.5f;
            float wCurve = my + 4.5f * (1.0f - lobe * lobe);
            for (int32_t y = y1; y <= y2; y++) {
                float dLine = fabsf(y - wCurve);
                if (dLine < 1.8f) {
                    float a = (1.8f - dLine) / 1.8f;
                    uint8_t r = (uint8_t)(BG_R + (mr - BG_R) * a);
                    uint8_t g = (uint8_t)(BG_G + (mg - BG_G) * a);
                    uint8_t b = (uint8_t)(BG_B + (mb - BG_B) * a);
                    putPixel(layer, x, y, r, g, b);
                } else if (open > 1.5f && fabsf(dx) < 7.0f && y > wCurve && y < wCurve + open * (1.0f - (dx * dx) / 49.0f)) {
                    putPixel(layer, x, y, 0xFF, 0x62, 0x98);
                }
            }
        }
    }
}

void CatFace::_drawCb(lv_event_t *e) {
    CatFace *self = (CatFace *)lv_event_get_user_data(e);
    self->_draw(lv_event_get_layer(e));
}

void CatFace::_draw(lv_layer_t *layer) const {
    for (int i = 0; i < 2; i++) {
        _rasterizeEye(layer, i, _cur[i]);
    }
    _rasterizeDecor(layer, lv_tick_get());
}
