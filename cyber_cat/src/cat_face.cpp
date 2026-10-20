#include "cat_face.h"
#include <Arduino.h>
#include <math.h>
#include <string.h>

// ==========================================================
//  Layout — 360×360 round screen
// ==========================================================
static const int16_t EYE_X[2] = {113, 247};
static const int16_t EYE_Y    = 135;
static const int16_t MOUTH_X  = 180;
static const int16_t MOUTH_Y  = 198;
static const int16_t BLUSH_DY = 68;      // below eye centre
static const int16_t BLUSH_DX = 10;      // pushed towards the outer side

#define COL_BG      lv_color_hex(0x0a0a1a)
#define COL_EYE     lv_color_hex(0xffffff)
#define COL_PUPIL   lv_color_hex(0x161630)
#define COL_BLUSH   lv_color_hex(0xff6f91)
#define COL_MOUTH   lv_color_hex(0xff8fa3)

static const float TAU = 6.2831853f;

// ==========================================================
//  Shapes
// ==========================================================
typedef CatFace::Pose Pose;

//                 dx   dy    w    h    lid    tilt   bot
static const Pose MOOD_POSE[MOOD_COUNT][2] = {
    /* NEUTRAL   */ {{0,  0,  96,  96, 0.00f,  0.00f, 0.00f}, {0,  0,  96,  96, 0.00f,  0.00f, 0.00f}},
    /* HAPPY     */ {{0, -6, 104,  92, 0.00f,  0.00f, 0.55f}, {0, -6, 104,  92, 0.00f,  0.00f, 0.55f}},
    /* CURIOUS   */ {{0, -5, 106, 106, 0.00f,  0.00f, 0.00f}, {0, -2,  94,  90, 0.16f, -0.08f, 0.00f}},
    /* CONFUSED  */ {{0,  3,  92,  78, 0.24f,  0.22f, 0.00f}, {0, -5, 104, 106, 0.00f,  0.00f, 0.00f}},
    /* SURPRISED */ {{0, -9, 112, 114, 0.00f,  0.00f, 0.00f}, {0, -9, 112, 114, 0.00f,  0.00f, 0.00f}},
};
static const Pose SLEEP_POSE = {0, 10, 84, 10, 0, 0, 0};

// Per-parameter spring tuning (dx, dy, w, h, lid, tilt, bot).
// Size springs are under-damped on purpose: a small overshoot reads as "alive".
static const float P_FREQ[CatFace::P_COUNT] = {4.5f, 4.5f, 4.0f, 4.0f, 6.0f, 5.0f, 5.0f};
static const float P_DAMP[CatFace::P_COUNT] = {0.70f, 0.70f, 0.58f, 0.58f, 0.90f, 0.85f, 0.70f};
static const float RIGHT_EYE_FREQ = 1.07f;   // slight lag between the eyes

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
    static const char *n[] = {"Neutral", "Happy", "Curious", "Confused", "Surprised"};
    static_assert(sizeof(n) / sizeof(n[0]) == MOOD_COUNT, "catMoodName out of sync");
    return m < MOOD_COUNT ? n[m] : "?";
}

static float frand(float lo, float hi) {
    return lo + (hi - lo) * (float)random(0, 10001) / 10000.0f;
}

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static int16_t r16(float v) { return (int16_t)lroundf(v); }

static bool elapsed(uint32_t now, uint32_t since, uint32_t ms) {
    return (uint32_t)(now - since) < ms;
}

// Implicit (backward Euler) update in ≤8ms sub-steps: stays stable even when a
// frame hitch hands us a large dt with a stiff, heavily damped spring.
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

bool CatFace::EyeGeom::operator!=(const EyeGeom &o) const {
    return memcmp(this, &o, sizeof(*this)) != 0;
}

bool CatFace::DecorGeom::operator!=(const DecorGeom &o) const {
    return memcmp(this, &o, sizeof(*this)) != 0;
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

    memset(_geom, 0, sizeof(_geom));
    memset(&_decorGeom, 0, sizeof(_decorGeom));

    for (int i = 0; i < 2; i++) {
        for (int p = 0; p < P_COUNT; p++) {
            _eye[i][p].freq = P_FREQ[p] * (i ? RIGHT_EYE_FREQ : 1.0f);
            _eye[i][p].damp = P_DAMP[p];
        }
        // Eyes grow from nothing on boot — the size springs give a pop-in.
        _eye[i][P_W].snap(0);
        _eye[i][P_H].snap(0);
    }
    _gazeX.freq = _gazeY.freq = 10.0f;
    _gazeX.damp = _gazeY.damp = 0.82f;
    _blushOpa.freq = 2.0f;   _blushOpa.damp = 1.0f;
    _mouthOpen.freq = 14.0f; _mouthOpen.damp = 0.9f;

    uint32_t now = lv_tick_get();
    _lastTickMs = now;
    _activitySinceMs = now;
    _nextGazeMs = now + 1500;
    _scheduleBlink(now + 800, false);

    _timer = lv_timer_create(_timerCb, 16, this);
}

void CatFace::setActivity(CatActivity a) {
    if (a == _activity) return;
    uint32_t now = lv_tick_get();
    CatActivity prev = _activity;
    _activity = a;
    _activitySinceMs = now;
    Serial.printf("[CatFace] %u activity %s -> %s\n", (unsigned)now, catActivityName(prev), catActivityName(a));

    if (prev == ACT_SLEEPING) {
        _wakeMs = now;
        _drowsy = 0;
        pulseMood(MOOD_SURPRISED, 380);
    }
    _nextGazeMs = now;
    _scheduleBlink(now, false);
}

void CatFace::setMood(CatMood m) {
    if (m != _mood) Serial.printf("[CatFace] mood %s\n", catMoodName(m));
    _mood = m;
}

void CatFace::pulseMood(CatMood m, uint32_t durationMs) {
    _pulseMood = m;
    _pulseUntilMs = lv_tick_get() + (durationMs ? durationMs : 1);
}

CatMood CatFace::mood() const {
    if (_pulseUntilMs && (int32_t)(lv_tick_get() - _pulseUntilMs) < 0) return _pulseMood;
    return _mood;
}

void CatFace::setDrowsiness(float d) { _drowsy = clampf(d, 0, 1); }
void CatFace::setSpeechLevel(uint16_t rms) { _speechRms = rms; }
void CatFace::setListenLevel(uint16_t rms) { _listenRms = rms; }

void CatFace::setTouch(bool pressed, int16_t x, int16_t y) {
    if (_touch && !pressed) _nextGazeMs = lv_tick_get() + 600;
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
//  Behaviour scheduling
// ==========================================================
void CatFace::_scheduleGaze(uint32_t now) {
    if (!_auto) {
        _gazeTgtX = _activity == ACT_THINKING ? -12.0f : 0;
        _gazeTgtY = _activity == ACT_THINKING ? -12.0f : 0;
        _nextGazeMs = now + 1000;
        return;
    }
    float hold;
    switch (_activity) {
    case ACT_IDLE:
        if (frand(0, 1) < 0.3f) { _gazeTgtX = 0; _gazeTgtY = 0; }
        else { _gazeTgtX = frand(-18, 18); _gazeTgtY = frand(-10, 8); }
        hold = frand(800, 3500);
        if (frand(0, 1) < 0.15f) _scheduleBlink(now, true);
        break;
    case ACT_THINKING:
        _thinkLeft = !_thinkLeft;
        _gazeTgtX = (_thinkLeft ? -1.0f : 1.0f) * frand(8, 14);
        _gazeTgtY = -frand(9, 14);
        hold = frand(1200, 2400);
        break;
    case ACT_LISTENING:
    case ACT_TALKING:
        _gazeTgtX = frand(-2, 2);
        _gazeTgtY = frand(-2, 2);
        hold = frand(500, 1500);
        break;
    default:
        _gazeTgtX = 0; _gazeTgtY = 0;
        hold = 1000;
        break;
    }
    _nextGazeMs = now + (uint32_t)hold;
}

// Picks the next blink. `soon` = blink within ~120ms (e.g. paired with a saccade).
void CatFace::_scheduleBlink(uint32_t now, bool soon) {
    if (!_auto) {
        _blinkStartMs = now + 0x40000000;
        _nextBlinkMs = now + 1000;
        return;
    }
    float lo = 2500, hi = 6000;
    switch (_activity) {
    case ACT_LISTENING: lo = 3500; hi = 7000; break;
    case ACT_THINKING:  lo = 2000; hi = 4500; break;
    case ACT_TALKING:   lo = 2500; hi = 5000; break;
    default: break;
    }
    uint32_t start = now + (uint32_t)(soon ? frand(0, 120) : frand(lo, hi));

    bool slow = (mood() == MOOD_HAPPY) && frand(0, 1) < 0.35f;
    _blinkCloseMs  = slow ? 220 : 70;
    _blinkOpenMs   = slow ? 320 : 130;
    _blinkOffsetMs = (uint8_t)frand(0, 18);
    _blinkTwice    = !slow && frand(0, 1) < 0.15f;
    _blinkStartMs  = start;

    uint32_t cycle = _blinkCloseMs + 40 + _blinkOpenMs;
    _nextBlinkMs = start + (_blinkTwice ? cycle * 2 + 60 : cycle) + _blinkOffsetMs;
}

float CatFace::_blinkAmount(uint32_t now, int eye) const {
    if (_activity == ACT_SLEEPING) return 0;
    int32_t t = (int32_t)(now - _blinkStartMs) - (eye ? _blinkOffsetMs : 0);
    if (t < 0) return 0;
    int32_t cycle = _blinkCloseMs + 40 + _blinkOpenMs;
    if (_blinkTwice && t >= cycle + 60) t -= cycle + 60;
    if (t < _blinkCloseMs) { float u = (float)t / _blinkCloseMs; return u * u; }
    t -= _blinkCloseMs;
    if (t < 40) return 1;
    t -= 40;
    if (t < _blinkOpenMs) { float u = 1.0f - (float)t / _blinkOpenMs; return u * u; }
    return 0;
}

// ==========================================================
//  Targets: mood shape → activity modifiers → interaction
// ==========================================================
void CatFace::_targets(uint32_t now, Pose out[2]) const {
    CatMood m = mood();
    uint32_t inAct = now - _activitySinceMs;

    for (int i = 0; i < 2; i++) {
        Pose p = MOOD_POSE[m][i];

        switch (_activity) {
        case ACT_LISTENING: {
            float k = clampf(_listenEnv / 2500.0f, 0, 1);
            float s = (inAct < 90) ? 0.86f : 1.06f + 0.07f * k;   // anticipation dip first
            p.w *= s; p.h *= s;
            p.dy -= 3;
            if (p.lid > 0.04f) p.lid = 0.04f;
            break;
        }
        case ACT_THINKING:
            if (p.lid < 0.30f) p.lid = 0.30f;
            p.tilt += i == 0 ? 0.20f : -0.06f;
            p.w -= 4;
            break;
        case ACT_SLEEPING: {
            p = SLEEP_POSE;
            float b = sinf(TAU * (float)now / 4800.0f);
            p.h += 3.0f * b;
            p.dy += 1.5f * b;
            break;
        }
        case ACT_IDLE:
            p.lid += 0.60f * _drowsy;
            p.tilt -= 0.25f * _drowsy;
            p.dy += 5.0f * _drowsy;
            p.h -= 10.0f * _drowsy;
            break;
        default:
            break;
        }

        // Waking up: the right eye stays shut a beat longer than the left.
        if (_wakeMs && i == 1 && elapsed(now, _wakeMs, 220)) p = SLEEP_POSE;

        if (_touch && _activity != ACT_SLEEPING) {
            p.h *= 0.90f; p.w *= 1.04f; p.dy += 2;
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

void CatFace::_tick() {
    uint32_t now = lv_tick_get();
    float dt = clampf((now - _lastTickMs) / 1000.0f, 0, 0.05f);
    _lastTickMs = now;

    if (_pulseUntilMs && (int32_t)(now - _pulseUntilMs) >= 0) _pulseUntilMs = 0;
    if (_wakeMs && !elapsed(now, _wakeMs, 2000)) _wakeMs = 0;

    // Level envelopes: fast attack, slower release.
    auto follow = [dt](float &env, float x, float attack, float release) {
        float tau = x > env ? attack : release;
        env += (x - env) * (1.0f - expf(-dt / tau));
    };
    follow(_speechEnv, _activity == ACT_TALKING ? _speechRms : 0, 0.03f, 0.12f);
    follow(_listenEnv, _activity == ACT_LISTENING ? _listenRms : 0, 0.05f, 0.20f);

    if ((int32_t)(now - _nextGazeMs) >= 0) _scheduleGaze(now);
    if ((int32_t)(now - _nextBlinkMs) >= 0) _scheduleBlink(now, false);

    Pose tg[2];
    _targets(now, tg);

    bool sleeping = _activity == ACT_SLEEPING;
    for (int i = 0; i < 2; i++) {
        const float *tv = &tg[i].dx;
        for (int p = 0; p < P_COUNT; p++) {
            Spring &s = _eye[i][p];
            float base = P_FREQ[p] * (i ? RIGHT_EYE_FREQ : 1.0f);
            s.freq = sleeping ? base * 0.45f : base;      // drift off slowly
            s.damp = sleeping ? 1.0f : P_DAMP[p];
            s.target = tv[p];
            s.step(dt);
        }
    }

    if (_touch && !sleeping) {
        _gazeX.target = clampf((_touchX - 180) / 180.0f * 18.0f, -18, 18);
        _gazeY.target = clampf((_touchY - 180) / 180.0f * 12.0f, -12, 12);
    } else {
        _gazeX.target = _gazeTgtX;
        _gazeY.target = _gazeTgtY;
    }
    _gazeX.step(dt);
    _gazeY.step(dt);

    float speech = _activity == ACT_TALKING ? clampf(sqrtf(_speechEnv / 4500.0f), 0, 1) : 0;
    CatMood m = mood();
    _blushOpa.target = (m == MOOD_HAPPY || _touch) ? 120 : 40;
    _mouthOpen.target = _activity == ACT_TALKING ? speech : (m == MOOD_SURPRISED ? 0.6f : 0);
    _blushOpa.step(dt);
    _mouthOpen.step(dt);

    EyeGeom geom[2];
    DecorGeom decor;
    _computeGeom(now, geom, decor);

    for (int i = 0; i < 2; i++) {
        if (geom[i] != _geom[i]) {
            _invalidate(_geom[i]);
            _invalidate(geom[i]);
            _geom[i] = geom[i];
        }
    }
    if (_decor && decor != _decorGeom) {
        _invalidateDecor(_decorGeom);
        _invalidateDecor(decor);
    }
    _decorGeom = decor;
}

void CatFace::_computeGeom(uint32_t now, EyeGeom out[2], DecorGeom &decor) {
    float gx = _gazeX.x, gy = _gazeY.x;
    float speech = _activity == ACT_TALKING ? clampf(sqrtf(_speechEnv / 4500.0f), 0, 1) : 0;
    bool breathing = _activity == ACT_IDLE || _activity == ACT_LISTENING || _activity == ACT_THINKING;
    float breath = breathing ? sinf(TAU * (float)now / 4200.0f) : 0;
    CatMood m = mood();
    float sumDx = 0, sumDy = 0;

    for (int i = 0; i < 2; i++) {
        const Spring *s = _eye[i];
        float dx = s[P_DX].x + gx, dy = s[P_DY].x + gy;
        float w = s[P_W].x, h = s[P_H].x;
        float lid = s[P_LID].x, tilt = s[P_TILT].x, bot = s[P_BOT].x;
        h *= 1.0f + 0.018f * breath;
        dy -= 1.0f * breath;

        // Looking sideways: the eye further from the gaze shrinks a little.
        float side = i == 0 ? -1.0f : 1.0f;
        float persp = 1.0f - 0.0035f * gx * side;
        w *= persp; h *= persp;

        if (speech > 0) {
            h *= 1.0f - 0.20f * speech;
            w *= 1.0f + 0.04f * speech;
            dy -= 4.0f * speech;
        }

        // Blink: squash towards a line, closing more from the top like a lid.
        float b = _blinkAmount(now, i);
        if (b > 0) {
            float h0 = h;
            h += (6.0f - h) * b;
            dy += (h0 - h) * 0.2f;
        }

        if (w < 6) w = 6;
        if (h < 4) h = 4;
        lid = clampf(lid, 0, 1);
        bot = clampf(bot, 0, 1);
        sumDx += dx; sumDy += dy;

        float cx = EYE_X[i] + dx, cy = EYE_Y + dy;
        EyeGeom &g = out[i];
        g.x1 = r16(cx - w / 2);
        g.x2 = g.x1 + (r16(w) > 1 ? r16(w) : 1) - 1;
        g.y1 = r16(cy - h / 2);
        g.y2 = g.y1 + (r16(h) > 1 ? r16(h) : 1) - 1;

        if (lid > 0.005f || fabsf(tilt) > 0.005f) {
            float base = g.y1 + lid * h;
            float delta = tilt * h * 0.5f;
            g.lidInner = r16(base + delta);
            g.lidOuter = r16(base - delta);
        } else {
            g.lidInner = g.lidOuter = g.y1 - 1;
        }

        if (bot > 0.01f) {
            float r = w * 0.85f;
            g.botR = r16(r);
            g.botCy = r16(g.y2 + r - bot * h * 1.05f);
        } else {
            g.botR = 0; g.botCy = 0;
        }

        g.px = g.py = g.pr = 0;
        if (_decor & DECOR_PUPILS) {
            float k = _activity == ACT_LISTENING ? 0.31f : (m == MOOD_SURPRISED ? 0.20f : 0.27f);
            float pr = k * (w < h ? w : h);
            if (pr >= 4) {
                float limX = w / 2 - pr - 4, limY = h / 2 - pr - 4;
                float ox = clampf(gx * 0.9f, -fmaxf(limX, 0), fmaxf(limX, 0));
                float oy = clampf(gy * 0.9f, -fmaxf(limY, 0), fmaxf(limY, 0));
                g.px = r16(cx + ox);
                g.py = r16(cy + oy);
                g.pr = r16(pr);
            }
        }
    }

    float avgDx = sumDx / 2, avgDy = sumDy / 2;
    decor.blushDx = r16(avgDx * 0.8f);
    decor.blushDy = r16(avgDy * 0.8f);
    decor.blushOpa = (uint8_t)clampf(_blushOpa.x, 0, 255);
    decor.mouthX = r16(MOUTH_X + avgDx * 0.7f);
    decor.mouthY = r16(MOUTH_Y + avgDy * 0.7f);
    decor.mouthR = m == MOOD_HAPPY ? 7 : 6;
    decor.mouthOpen = r16(clampf(_mouthOpen.x, 0, 1) * 12.0f);
}

void CatFace::_invalidate(const EyeGeom &g) {
    if (!_obj || g.x2 <= g.x1) return;
    lv_area_t a = {(int32_t)(g.x1 - 3), (int32_t)(g.y1 - 3), (int32_t)(g.x2 + 3), (int32_t)(g.y2 + 3)};
    lv_obj_invalidate_area(_obj, &a);
}

void CatFace::_invalidateDecor(const DecorGeom &d) {
    if (!_obj) return;
    for (int i = 0; i < 2; i++) {
        int16_t side = i == 0 ? -1 : 1;
        int16_t cx = EYE_X[i] + side * BLUSH_DX + d.blushDx;
        int16_t cy = EYE_Y + BLUSH_DY + d.blushDy;
        lv_area_t a = {cx - 28, cy - 14, cx + 28, cy + 14};
        lv_obj_invalidate_area(_obj, &a);
    }
    lv_area_t m = {d.mouthX - 2 * d.mouthR - 6, d.mouthY - 4,
                   d.mouthX + 2 * d.mouthR + 6, d.mouthY + d.mouthR + 18};
    lv_obj_invalidate_area(_obj, &m);
}

// ==========================================================
//  Drawing
// ==========================================================
static void fillRect(lv_layer_t *layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                     lv_color_t c, lv_opa_t opa, int32_t radius) {
    if (x2 < x1 || y2 < y1) return;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = c;
    d.bg_opa = opa;
    d.radius = radius;
    lv_area_t a = {x1, y1, x2, y2};
    lv_draw_rect(layer, &d, &a);
}

static bool intersect(lv_area_t *out, const lv_area_t *a, const lv_area_t *b) {
    out->x1 = LV_MAX(a->x1, b->x1);
    out->y1 = LV_MAX(a->y1, b->y1);
    out->x2 = LV_MIN(a->x2, b->x2);
    out->y2 = LV_MIN(a->y2, b->y2);
    return out->x1 <= out->x2 && out->y1 <= out->y2;
}

void CatFace::_drawEye(lv_layer_t *layer, int eye, const EyeGeom &g) {
    const int16_t x1 = g.x1, y1 = g.y1, x2 = g.x2, y2 = g.y2;
    const int16_t lidOuter = g.lidOuter, lidInner = g.lidInner;
    const int16_t botCy = g.botCy, botR = g.botR, px = g.px, py = g.py, pr = g.pr;
    lv_area_t area = {x1, y1, x2, y2};
    lv_area_t clip;
    if (!intersect(&clip, &layer->_clip_area, &area)) return;
    lv_area_t saved = layer->_clip_area;
    layer->_clip_area = clip;

    fillRect(layer, x1, y1, x2, y2, COL_EYE, LV_OPA_COVER, LV_RADIUS_CIRCLE);

    if (pr > 0) {
        fillRect(layer, px - pr, py - pr, px + pr - 1, py + pr - 1, COL_PUPIL, LV_OPA_COVER, LV_RADIUS_CIRCLE);
        int16_t hr = pr * 32 / 100 > 2 ? pr * 32 / 100 : 2;
        int16_t hx = px - pr * 35 / 100, hy = py - pr * 38 / 100;
        fillRect(layer, hx - hr, hy - hr, hx + hr - 1, hy + hr - 1, COL_EYE, LV_OPA_COVER, LV_RADIUS_CIRCLE);
    }

    // Upper lid: background-coloured cover above a straight (possibly tilted) edge.
    // Left eye's inner corner is on its right; mirrored for the right eye.
    int16_t yLeft  = eye == 0 ? lidOuter : lidInner;
    int16_t yRight = eye == 0 ? lidInner : lidOuter;
    int16_t yMin = yLeft < yRight ? yLeft : yRight;
    int16_t yMax = yLeft > yRight ? yLeft : yRight;
    if (yMax > y1) {
        if (yMin > y1) fillRect(layer, x1, y1, x2, yMin - 1, COL_BG, LV_OPA_COVER, 0);
        if (yMax > yMin) {
            lv_draw_triangle_dsc_t t;
            lv_draw_triangle_dsc_init(&t);
            t.color = COL_BG;
            t.opa = LV_OPA_COVER;
            t.p[0].x = x1;     t.p[0].y = yMin;
            t.p[1].x = x2 + 1; t.p[1].y = yMin;
            if (yLeft > yRight) { t.p[2].x = x1;     t.p[2].y = yLeft; }
            else                { t.p[2].x = x2 + 1; t.p[2].y = yRight; }
            lv_draw_triangle(layer, &t);
        }
    }

    // Lower lid: a big background circle pushed up from below → crescent "^".
    if (botR > 0) {
        int16_t cx = (x1 + x2) / 2;
        fillRect(layer, cx - botR, botCy - botR, cx + botR, botCy + botR, COL_BG, LV_OPA_COVER, LV_RADIUS_CIRCLE);
    }

    layer->_clip_area = saved;
}

void CatFace::_drawCb(lv_event_t *e) {
    CatFace *self = (CatFace *)lv_event_get_user_data(e);
    self->_draw(lv_event_get_layer(e));
}

void CatFace::_draw(lv_layer_t *layer) const {
    for (int i = 0; i < 2; i++) {
        const EyeGeom &g = _geom[i];
        if (g.x2 <= g.x1) continue;
        _drawEye(layer, i, g);
    }

    const DecorGeom &d = _decorGeom;
    if ((_decor & DECOR_BLUSH) && d.blushOpa > 2) {
        for (int i = 0; i < 2; i++) {
            int16_t side = i == 0 ? -1 : 1;
            int16_t cx = EYE_X[i] + side * BLUSH_DX + d.blushDx;
            int16_t cy = EYE_Y + BLUSH_DY + d.blushDy;
            fillRect(layer, cx - 24, cy - 11, cx + 23, cy + 10, COL_BLUSH, d.blushOpa / 2, LV_RADIUS_CIRCLE);
            fillRect(layer, cx - 16, cy - 7, cx + 15, cy + 6, COL_BLUSH, d.blushOpa, LV_RADIUS_CIRCLE);
        }
    }

    if (_decor & DECOR_MOUTH) {
        if (d.mouthOpen >= 2) {
            int16_t hw = 4 + d.mouthOpen / 3;
            fillRect(layer, d.mouthX - hw, d.mouthY + 2, d.mouthX + hw, d.mouthY + 2 + d.mouthOpen,
                     COL_MOUTH, LV_OPA_90, LV_RADIUS_CIRCLE);
        }
        lv_draw_arc_dsc_t a;
        lv_draw_arc_dsc_init(&a);
        a.color = COL_EYE;
        a.opa = LV_OPA_90;
        a.width = 3;
        a.rounded = 1;
        a.radius = d.mouthR;
        a.start_angle = 0;
        a.end_angle = 180;
        a.center.x = d.mouthX - d.mouthR + 1; a.center.y = d.mouthY;
        lv_draw_arc(layer, &a);
        a.center.x = d.mouthX + d.mouthR - 1;
        lv_draw_arc(layer, &a);
    }
}
