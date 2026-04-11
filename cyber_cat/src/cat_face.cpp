#include "cat_face.h"
#include "Touch_CST816.h"
#include <Arduino.h>

// ==========================================================
//  Layout — 360×360 round screen, minimalist style
// ==========================================================
#define CX 180
#define CY 180

#define EYE_L_X  113
#define EYE_R_X  247
#define EYE_Y    135
#define EYE_D    95

// ==========================================================
//  Colors
// ==========================================================
#define COL_BG  lv_color_hex(0x0a0a1a)
#define COL_EYE lv_color_hex(0xffffff)

// ==========================================================
//  Animation timing (ms)
// ==========================================================
#define EXPR_DUR          350
#define BLINK_CLOSE_DUR   100
#define BLINK_OPEN_DUR    120
#define HAPPY_BLINK_CLOSE 350
#define HAPPY_BLINK_OPEN  400
#define LOOK_MOVE_DUR     250
#define LOOK_HOLD_DUR    1000
#define LISTEN_PULSE_CLOSE 150   // Listening: subtle eye pulse (not a full blink)
#define LISTEN_PULSE_OPEN  200

// ==========================================================
//  Talking — 4-stage squeeze/unsquash loop
//
//  Stage 0  Neutral → squeeze    100ms ease_out
//  Stage 1  squeeze → unsquash   150ms ease_out
//  Stage 2  unsquash → overshoot 100ms overshoot (approx. ease_in_out)
//  Stage 3  overshoot → neutral  100ms linear
//
//  Each stage is a lerp from one EyeState to the next.
//  Left and right eyes are mirror images (rot sign flipped).
// ==========================================================
struct TalkStage {
    CatFace::EyeState toL;   // target left eye
    CatFace::EyeState toR;   // target right eye (rot is already negated)
    uint32_t          dur;   // ms
    lv_anim_path_cb_t path;
};

// Neutral baseline for Talking (cx/cy are absolute; rot = 0, look ignored here)
static const CatFace::EyeState TALK_BASE_L = {EYE_L_X, EYE_Y, EYE_D, EYE_D, 0};
static const CatFace::EyeState TALK_BASE_R = {EYE_R_X, EYE_Y, EYE_D, EYE_D, 0};

// Stage table — row order matches stage index 0-3
// rot unit: 0.1 degrees (LVGL convention)
static const TalkStage TALK_STAGES[4] = {
    // Stage 0: squeeze — flatten & tilt inward, lift up
    { {EYE_L_X, (int16_t)(EYE_Y - 10), 100, 60,  100},
      {EYE_R_X, (int16_t)(EYE_Y - 10), 100, 60, -100},
      100, lv_anim_path_ease_out },

    // Stage 1: unsquash — expand back beyond neutral, settle down
    { {EYE_L_X, (int16_t)(EYE_Y + 10), 95, 95,  50},
      {EYE_R_X, (int16_t)(EYE_Y + 10), 95, 95, -50},
      150, lv_anim_path_ease_out },

    // Stage 2: overshoot — very slightly larger, fully upright
    { {EYE_L_X, EYE_Y, 98, 98, 0},
      {EYE_R_X, EYE_Y, 98, 98, 0},
      100, lv_anim_path_ease_in_out },

    // Stage 3: settle back to neutral
    { TALK_BASE_L, TALK_BASE_R,
      100, lv_anim_path_linear },
};

// ==========================================================
CatFace catFace;

const char *catExprName(CatExpression e) {
    static const char *n[] = {
        "Neutral", "Happy", "Listening", "Thinking", "Sleepy", "Talking"
    };
    static_assert(sizeof(n) / sizeof(n[0]) == CAT_EXPR_COUNT,
                  "catExprName: array out of sync with CAT_EXPR_COUNT");
    return e < CAT_EXPR_COUNT ? n[e] : "?";
}

static int32_t rng(int32_t lo, int32_t hi) {
    if (lo >= hi) return lo;
    return random(lo, hi);
}

static int16_t mix16(int16_t a, int16_t b, int32_t t) {
    return (int16_t)(a + (int32_t)(b - a) * t / 1000);
}

CatFace::EyeState CatFace::_lerpState(const EyeState &a,
                                       const EyeState &b,
                                       int32_t t) {
    return {
        mix16(a.cx, b.cx, t), mix16(a.cy, b.cy, t),
        mix16(a.w,  b.w,  t), mix16(a.h,  b.h,  t),
        mix16(a.rot, b.rot, t)
    };
}

// ==========================================================
//  Place one eye
// ==========================================================
void CatFace::_placeEye(lv_obj_t *eye, const EyeState &st,
                        int16_t dx, int16_t dy) {
    int16_t w = st.w > 0 ? st.w : 1;
    int16_t h = st.h > 0 ? st.h : 1;
    lv_obj_set_size(eye, w, h);
    lv_obj_set_style_radius(eye, (w < h ? w : h) / 2, 0);
    lv_obj_set_pos(eye, st.cx + dx - w / 2,
                        st.cy + dy - h / 2);
    lv_obj_set_style_transform_rotation(eye, st.rot, 0);
}

// ==========================================================
//  Central render — expression morph + blink composite
//  (look offset suppressed during talking so eyes stay centred)
// ==========================================================
void CatFace::_render() {
    _baseL = _lerpState(_fromL, _stL, _exprProgress);
    _baseR = _lerpState(_fromR, _stR, _exprProgress);

    EyeState rL = _baseL;
    EyeState rR = _baseR;

    if (_blinkFactor > 0) {
        if (_cur == CAT_HAPPY) {
            rL.h = mix16(rL.h, 4, _blinkFactor);
            rR.h = mix16(rR.h, 4, _blinkFactor);
        } else if (_cur == CAT_LISTENING) {
            // Pronounced pulse: shrinks to ~60% of base size
            rL.h = mix16(rL.h, 70, _blinkFactor);
            rR.h = mix16(rR.h, 65, _blinkFactor);
        } else {
            rL.h = mix16(rL.h, 6, _blinkFactor);
            rR.h = mix16(rR.h, 6, _blinkFactor);
        }
    }

    int16_t lx = (_cur == CAT_TALKING || _cur == CAT_LISTENING) ? 0 : _lookDX;
    int16_t ly = (_cur == CAT_TALKING || _cur == CAT_LISTENING) ? 0 : _lookDY;
    _placeEye(_eyeL, rL, lx, ly);
    _placeEye(_eyeR, rR, lx, ly);
}

// ==========================================================
//  Expression morph callback
// ==========================================================
void CatFace::_exprAnimCb(void *var, int32_t v) {
    CatFace *self = (CatFace *)var;
    self->_exprProgress = v;
    self->_render();
}

// ==========================================================
//  Blink callbacks
// ==========================================================
void CatFace::_blinkAnimCb(void *var, int32_t v) {
    CatFace *self = (CatFace *)var;
    if (self->_cur == CAT_SLEEPY) return;
    self->_blinkFactor = v;
    self->_render();
}

void CatFace::_blinkDoneCb(lv_anim_t *a) {
    CatFace *self = (CatFace *)a->var;
    if ((uintptr_t)lv_anim_get_user_data(a) != self->_animEpoch) return;
    if (self->_cur == CAT_SLEEPY) return;
    self->_blinkFactor = 0;
    self->_render();
    self->_scheduleBlink();
}

void CatFace::_scheduleBlink() {
    if (_cur == CAT_SLEEPY || _cur == CAT_TALKING) return;
    bool happy     = (_cur == CAT_HAPPY);
    bool listening = (_cur == CAT_LISTENING);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, this);
    lv_anim_set_user_data(&a, (void *)(uintptr_t)_animEpoch);
    lv_anim_set_values(&a, 0, 1000);
    if (listening) {
        // Subtle heartbeat pulse — faster, gentler than a blink
        lv_anim_set_duration(&a, LISTEN_PULSE_CLOSE);
        lv_anim_set_reverse_duration(&a, LISTEN_PULSE_OPEN);
        lv_anim_set_delay(&a, rng(600, 900));
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    } else {
        lv_anim_set_duration(&a, happy ? HAPPY_BLINK_CLOSE : BLINK_CLOSE_DUR);
        lv_anim_set_reverse_duration(&a, happy ? HAPPY_BLINK_OPEN : BLINK_OPEN_DUR);
        lv_anim_set_delay(&a, happy ? rng(2500, 5000) : rng(4000, 10000));
        lv_anim_set_path_cb(&a, happy ? lv_anim_path_ease_in_out : lv_anim_path_ease_out);
    }
    lv_anim_set_exec_cb(&a, _blinkAnimCb);
    lv_anim_set_completed_cb(&a, _blinkDoneCb);
    lv_anim_start(&a);
}

// ==========================================================
//  Look-around callbacks
// ==========================================================
void CatFace::_lookAnimCb(void *var, int32_t v) {
    CatFace *self = (CatFace *)var;
    if (self->_cur == CAT_SLEEPY || self->_cur == CAT_TALKING ||
        self->_cur == CAT_LISTENING) return;
    self->_lookDX = mix16(0, self->_lookTgtDX, v);
    self->_lookDY = mix16(0, self->_lookTgtDY, v);
    self->_render();
}

void CatFace::_lookDoneCb(lv_anim_t *a) {
    CatFace *self = (CatFace *)a->var;
    if ((uintptr_t)lv_anim_get_user_data(a) != self->_animEpoch) return;
    self->_lookDX = 0;
    self->_lookDY = 0;
    self->_render();
    self->_scheduleLook();
}

void CatFace::_scheduleLook() {
    if (_cur == CAT_HAPPY || _cur == CAT_TALKING || _cur == CAT_LISTENING) return;

    // Thinking: reduced amplitude — eyes drift slightly, not widely
    bool thinking = (_cur == CAT_THINKING);
    _lookTgtDX = (int16_t)rng(thinking ? -4 : -8, thinking ? 5 : 9);
    _lookTgtDY = (int16_t)rng(thinking ? -3 : -5, thinking ? 4 : 6);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, this);
    lv_anim_set_user_data(&a, (void *)(uintptr_t)_animEpoch);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, LOOK_MOVE_DUR);
    lv_anim_set_reverse_duration(&a, LOOK_MOVE_DUR);
    lv_anim_set_reverse_delay(&a, LOOK_HOLD_DUR);
    lv_anim_set_delay(&a, rng(3000, 6000));
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, _lookAnimCb);
    lv_anim_set_completed_cb(&a, _lookDoneCb);
    lv_anim_start(&a);
}

// ==========================================================
//  Talking — squeeze/unsquash loop
//
//  _talkExecCbL / R: exec callbacks for left and right eye.
//  Each receives t = 0..1000 and interpolates from
//  the previous stage's target to the current stage's target.
//
//  The stage table holds absolute targets; the "from" state is
//  always the previous stage's "to" (wrapping stage 3 → stage 0).
// ==========================================================
void CatFace::_talkExecCbL(void *var, int32_t t) {
    CatFace *self = (CatFace *)var;
    if (self->_talkStage < 0) return;

    int8_t  stage  = self->_talkStage;
    int8_t  prev   = (stage == 0) ? 3 : stage - 1;
    EyeState from  = TALK_STAGES[prev].toL;
    EyeState to    = TALK_STAGES[stage].toL;
    EyeState cur   = _lerpState(from, to, t);

    // Composite with blink — no look offset during talking
    if (self->_blinkFactor > 0)
        cur.h = mix16(cur.h, 6, self->_blinkFactor);

    self->_placeEye(self->_eyeL, cur);
}

void CatFace::_talkExecCbR(void *var, int32_t t) {
    CatFace *self = (CatFace *)var;
    if (self->_talkStage < 0) return;

    int8_t  stage  = self->_talkStage;
    int8_t  prev   = (stage == 0) ? 3 : stage - 1;
    EyeState from  = TALK_STAGES[prev].toR;
    EyeState to    = TALK_STAGES[stage].toR;
    EyeState cur   = _lerpState(from, to, t);

    if (self->_blinkFactor > 0)
        cur.h = mix16(cur.h, 6, self->_blinkFactor);

    self->_placeEye(self->_eyeR, cur);
}

// Called when both left AND right animations for a stage complete.
// We use the RIGHT eye's done callback as the master trigger for
// advancing the stage (left and right run identical durations).
void CatFace::_talkDoneCb(lv_anim_t *a) {
    CatFace *self = (CatFace *)a->var;

    // Epoch guard — discard if _apply() ran while we were in flight
    if ((uintptr_t)lv_anim_get_user_data(a) != self->_animEpoch) return;
    if (self->_talkStage < 0) return;

    // Advance to next stage (wrap 3 → 0 for infinite loop)
    int8_t next = (self->_talkStage + 1) % 4;
    self->_startTalkStage(next);
}

void CatFace::_startTalkStage(int stage, uint32_t delay) {
    _talkStage = (int8_t)stage;
    const TalkStage &s = TALK_STAGES[stage];

    // Re-use the two pre-allocated lv_anim_t buffers; LVGL copies
    // them internally on lv_anim_start so the struct can be stack-local,
    // but we keep them as members to avoid any inadvertent aliasing.
    lv_anim_init(&_talkAnimL);
    lv_anim_set_var(&_talkAnimL, this);
    lv_anim_set_user_data(&_talkAnimL, (void *)(uintptr_t)_animEpoch);
    lv_anim_set_values(&_talkAnimL, 0, 1000);
    lv_anim_set_duration(&_talkAnimL, s.dur);
    lv_anim_set_delay(&_talkAnimL, delay);
    lv_anim_set_path_cb(&_talkAnimL, s.path);
    lv_anim_set_exec_cb(&_talkAnimL, _talkExecCbL);
    // Left eye: no done callback — driven by right master
    lv_anim_start(&_talkAnimL);

    lv_anim_init(&_talkAnimR);
    lv_anim_set_var(&_talkAnimR, this);
    lv_anim_set_user_data(&_talkAnimR, (void *)(uintptr_t)_animEpoch);
    lv_anim_set_values(&_talkAnimR, 0, 1000);
    lv_anim_set_duration(&_talkAnimR, s.dur);
    lv_anim_set_delay(&_talkAnimR, delay);
    lv_anim_set_path_cb(&_talkAnimR, s.path);
    lv_anim_set_exec_cb(&_talkAnimR, _talkExecCbR);
    lv_anim_set_completed_cb(&_talkAnimR, _talkDoneCb);  // right is master
    lv_anim_start(&_talkAnimR);
}

void CatFace::_startTalking(uint32_t initialDelay) {
    _talkStage = -1;    // will be set by _startTalkStage
    _startTalkStage(0, initialDelay);
}

void CatFace::_stopTalking() {
    _talkStage = -1;
    lv_anim_delete(this, _talkExecCbL);
    lv_anim_delete(this, _talkExecCbR);
}

// ==========================================================
//  Apply expression — starts smooth morph animation
// ==========================================================
void CatFace::_apply(CatExpression e) {
    // Stop everything from the previous expression
    _stopTalking();
    lv_anim_delete(this, _exprAnimCb);
    lv_anim_delete(this, _blinkAnimCb);
    lv_anim_delete(this, _lookAnimCb);

    _animEpoch++;
    _blinkFactor = 0;
    _lookDX = 0;
    _lookDY = 0;

    _fromL = _baseL;
    _fromR = _baseR;

    _cur = e;

    switch (e) {
    case CAT_NEUTRAL:
        _stL = {EYE_L_X, EYE_Y, EYE_D, EYE_D, 0};
        _stR = {EYE_R_X, EYE_Y, EYE_D, EYE_D, 0};
        break;

    case CAT_HAPPY:
        _stL = {EYE_L_X, (int16_t)(EYE_Y - 5), 95, 55, 0};
        _stR = {EYE_R_X, (int16_t)(EYE_Y - 5), 95, 55, 0};
        break;

    case CAT_LISTENING:
        // Enlarged + asymmetric — clearly different from Neutral
        // Left eye notably bigger, both shifted up to look alert
        _stL = {EYE_L_X,      (int16_t)(EYE_Y - 8), 118, 118, 0};
        _stR = {(int16_t)(EYE_R_X + 3), (int16_t)(EYE_Y - 6), 110, 110, 0};
        break;

    case CAT_THINKING:
        // Slightly narrowed + outward-shifted + subtle tilt
        // Conveys "eyes unfocused, gazing into the distance"
        _stL = {(int16_t)(EYE_L_X - 2), EYE_Y, 88, 78, -50};
        _stR = {(int16_t)(EYE_R_X + 2), EYE_Y, 88, 78,  50};
        break;

    case CAT_SLEEPY:
        _stL = {EYE_L_X, EYE_Y, 80, 8, 0};
        _stR = {EYE_R_X, EYE_Y, 80, 8, 0};
        break;

    case CAT_TALKING:
        // Eyes hold neutral geometry; the talk-loop animates on top
        _stL = TALK_BASE_L;
        _stR = TALK_BASE_R;
        break;

    default: break;
    }

    // Kick off the expression morph (0 → 1000 over EXPR_DUR ms)
    _exprProgress = 0;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, this);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, EXPR_DUR);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, _exprAnimCb);
    lv_anim_start(&a);

    if (e == CAT_TALKING) {
        // Delay so the squeeze loop starts only after the morph settles
        _startTalking(EXPR_DUR);
    }

    _scheduleBlink();
    _scheduleLook();
}

// ==========================================================
//  Create
// ==========================================================
void CatFace::create(lv_obj_t *parent) {
    _scr = parent;
    lv_obj_set_style_bg_color(_scr, COL_BG, 0);
    lv_obj_set_style_bg_opa(_scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(_scr, LV_OBJ_FLAG_SCROLLABLE);

    _eyeL = lv_obj_create(_scr);
    lv_obj_remove_style_all(_eyeL);
    lv_obj_set_style_bg_color(_eyeL, COL_EYE, 0);
    lv_obj_set_style_bg_opa(_eyeL, LV_OPA_COVER, 0);
    lv_obj_remove_flag(_eyeL,
        (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));

    _eyeR = lv_obj_create(_scr);
    lv_obj_remove_style_all(_eyeR);
    lv_obj_set_style_bg_color(_eyeR, COL_EYE, 0);
    lv_obj_set_style_bg_opa(_eyeR, LV_OPA_COVER, 0);
    lv_obj_remove_flag(_eyeR,
        (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));

    // Eyes grow from zero on boot — pop-in effect
    _baseL = {EYE_L_X, EYE_Y, 0, 0, 0};
    _baseR = {EYE_R_X, EYE_Y, 0, 0, 0};

    _lastTouchMs = millis();    // idle timer starts from creation
    _apply(CAT_NEUTRAL);
}

// ==========================================================
//  Update — gesture handling (animations are self-driving)
// ==========================================================
void CatFace::update() {
    uint32_t now = lv_tick_get();

    GESTURE g = touch_data.gesture;

    if (g != NONE && g != _prevGesture) {
        Serial.printf("[CatFace] gesture=%d\n", (int)g);
        _lastTouchMs = millis();

        if (g == LONG_PRESS) {
            Serial.println("[CatFace] Long press");
            // Wake from SLEEPY first — caller handles the chat trigger
            if (_cur == CAT_SLEEPY) {
                _apply(CAT_NEUTRAL);
            }
            if (_longPressCb) _longPressCb();
        } else if (g == SINGLE_CLICK) {
            // Wake from SLEEPY on tap
            if (_cur == CAT_SLEEPY) {
                _apply(CAT_NEUTRAL);
            } else {
                // Allow caller (main.cpp) to intercept the tap
                bool consumed = _tapCb ? _tapCb() : false;
                if (!consumed) {
                    if (_cur != CAT_HAPPY) _baseExpr = _cur;
                    _touchHappyEnd = now + 2000;
                    Serial.println("[CatFace] Tap -> Happy");
                    _apply(CAT_HAPPY);
                }
            }
        }
    }
    _prevGesture = g;

    if (_touchHappyEnd > 0 && now >= _touchHappyEnd) {
        _touchHappyEnd = 0;
        _apply(_baseExpr);
    }
}

// ==========================================================
//  Public API
// ==========================================================
void CatFace::setExpression(CatExpression e) {
    _baseExpr = e;
    _touchHappyEnd = 0;
    Serial.printf("[CatFace] -> %s\n", catExprName(e));
    _apply(e);
}
