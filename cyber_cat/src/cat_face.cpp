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
#define EYE_Y    155
#define EYE_D    95

#define MOUTH_X  CX
#define MOUTH_Y  265

// ==========================================================
//  Colors
// ==========================================================
#define COL_BG    lv_color_hex(0x0a0a1a)
#define COL_EYE   lv_color_hex(0xffffff)
#define COL_MOUTH lv_color_hex(0xffffff)

// ==========================================================
//  Happy crescent: mask circle sits above eye to reveal ◡ arc
// ==========================================================
#define HAPPY_EYE_D    80
#define MASK_DY_ON      20   // mask final position: 20px below eye center → ◠ crescent
#define MASK_DY_OFF     82   // mask off-screen: below eye, no coverage
#define MASK_PAD         2

// ==========================================================
//  Animation timing (ms)
// ==========================================================
#define EXPR_DUR          350
#define BLINK_CLOSE_DUR   100
#define BLINK_OPEN_DUR    120
#define LOOK_MOVE_DUR     250
#define LOOK_HOLD_DUR    1000
#define MOUTH_DUR         180

// ==========================================================
CatFace catFace;

const char *catExprName(CatExpression e) {
    static const char *n[] = {
        "Neutral", "Happy", "Sad", "Angry",
        "Sleepy", "Curious", "Talking"
    };
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
//  Place one eye at given state + optional look offset
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
//  Central render — composites expression + blink + look
// ==========================================================
void CatFace::_render() {
    _baseL = _lerpState(_fromL, _stL, _exprProgress);
    _baseR = _lerpState(_fromR, _stR, _exprProgress);

    EyeState rL = _baseL;
    EyeState rR = _baseR;

    if (_blinkFactor > 0) {
        rL.h = mix16(rL.h, 6, _blinkFactor);
        rR.h = mix16(rR.h, 6, _blinkFactor);
    }

    _placeEye(_eyeL, rL, _lookDX, _lookDY);
    _placeEye(_eyeR, rR, _lookDX, _lookDY);

    // Slide happy crescent masks (track interpolated eye state, stay unrotated)
    // Masks stay axis-aligned; MASK_PAD is large enough to cover rotated eye
    if (_maskDY < MASK_DY_OFF) {
        int16_t mw, mh, mr;

        mw = _baseL.w + MASK_PAD;
        mh = _baseL.h + MASK_PAD;
        mr = (mw < mh ? mw : mh) / 2;
        lv_obj_set_size(_maskL, mw, mh);
        lv_obj_set_style_radius(_maskL, mr, 0);
        lv_obj_set_pos(_maskL, _baseL.cx + _lookDX - mw / 2,
                               _baseL.cy + _lookDY + _maskDY - mh / 2);

        mw = _baseR.w + MASK_PAD;
        mh = _baseR.h + MASK_PAD;
        mr = (mw < mh ? mw : mh) / 2;
        lv_obj_set_size(_maskR, mw, mh);
        lv_obj_set_style_radius(_maskR, mr, 0);
        lv_obj_set_pos(_maskR, _baseR.cx + _lookDX - mw / 2,
                               _baseR.cy + _lookDY + _maskDY - mh / 2);
    }
}

// ==========================================================
//  Animation callbacks
// ==========================================================
void CatFace::_exprAnimCb(void *var, int32_t v) {
    CatFace *self = (CatFace *)var;
    self->_exprProgress = v;
    self->_render();
}

void CatFace::_blinkAnimCb(void *var, int32_t v) {
    CatFace *self = (CatFace *)var;
    if (self->_cur == CAT_HAPPY || self->_cur == CAT_SLEEPY) return;
    self->_blinkFactor = v;
    self->_render();
}

void CatFace::_blinkDoneCb(lv_anim_t *a) {
    CatFace *self = (CatFace *)a->var;
    self->_blinkFactor = 0;
    self->_render();
    self->_scheduleBlink();
}

void CatFace::_lookAnimCb(void *var, int32_t v) {
    CatFace *self = (CatFace *)var;
    if (self->_cur == CAT_HAPPY || self->_cur == CAT_SLEEPY) return;
    self->_lookDX = mix16(0, self->_lookTgtDX, v);
    self->_lookDY = mix16(0, self->_lookTgtDY, v);
    self->_render();
}

void CatFace::_lookDoneCb(lv_anim_t *a) {
    CatFace *self = (CatFace *)a->var;
    self->_lookDX = 0;
    self->_lookDY = 0;
    self->_render();
    self->_scheduleLook();
}

void CatFace::_mouthAnimCb(void *var, int32_t v) {
    CatFace *self = (CatFace *)var;
    int16_t mw = mix16(14, 24, v);
    int16_t mh = mix16(10, 20, v);
    lv_obj_set_size(self->_mouth, mw, mh);
    lv_obj_set_style_radius(self->_mouth, (mw < mh ? mw : mh) / 2, 0);
    lv_obj_set_pos(self->_mouth, MOUTH_X - mw / 2, MOUTH_Y - mh / 2);
}

void CatFace::_maskAnimCb(void *var, int32_t v) {
    CatFace *self = (CatFace *)var;
    self->_maskDY = (int16_t)v;
    self->_render();
}

void CatFace::_maskFadeDoneCb(lv_anim_t *a) {
    CatFace *self = (CatFace *)a->var;
    if (self->_maskDY >= MASK_DY_OFF) {
        lv_obj_add_flag(self->_maskL, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(self->_maskR, LV_OBJ_FLAG_HIDDEN);
    }
}

// ==========================================================
//  Schedule self-repeating blink
// ==========================================================
void CatFace::_scheduleBlink() {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, this);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, BLINK_CLOSE_DUR);
    lv_anim_set_reverse_duration(&a, BLINK_OPEN_DUR);
    lv_anim_set_delay(&a, rng(4000, 10000));
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, _blinkAnimCb);
    lv_anim_set_completed_cb(&a, _blinkDoneCb);
    lv_anim_start(&a);
}

// ==========================================================
//  Schedule self-repeating idle look-around
// ==========================================================
void CatFace::_scheduleLook() {
    _lookTgtDX = (int16_t)rng(-8, 9);
    _lookTgtDY = (int16_t)rng(-5, 6);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, this);
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
//  Apply expression — starts smooth morph animation
// ==========================================================
void CatFace::_apply(CatExpression e) {
    lv_anim_delete(this, _exprAnimCb);
    lv_anim_delete(this, _blinkAnimCb);
    lv_anim_delete(this, _lookAnimCb);
    lv_anim_delete(this, _mouthAnimCb);
    lv_anim_delete(this, _maskAnimCb);

    _blinkFactor = 0;
    _lookDX = 0;
    _lookDY = 0;

    _fromL = _baseL;
    _fromR = _baseR;
    _cur = e;

    lv_obj_add_flag(_mouth, LV_OBJ_FLAG_HIDDEN);

    switch (e) {
    case CAT_NEUTRAL:
        _stL = {EYE_L_X, EYE_Y, EYE_D, EYE_D, 0};
        _stR = {EYE_R_X, EYE_Y, EYE_D, EYE_D, 0};
        break;

    case CAT_HAPPY:
        _stL = {EYE_L_X, EYE_Y, HAPPY_EYE_D, HAPPY_EYE_D, 0};
        _stR = {EYE_R_X, EYE_Y, HAPPY_EYE_D, HAPPY_EYE_D, 0};
        break;

    case CAT_SAD:
        _stL = {(int16_t)(EYE_L_X - 3), (int16_t)(EYE_Y + 12), 75, 75, 0};
        _stR = {(int16_t)(EYE_R_X + 3), (int16_t)(EYE_Y + 12), 75, 75, 0};
        break;

    case CAT_ANGRY:
        _stL = {EYE_L_X, EYE_Y, 70, 35, -200};
        _stR = {EYE_R_X, EYE_Y, 70, 35,  200};
        break;

    case CAT_SLEEPY:
        _stL = {EYE_L_X, EYE_Y, 80, 8, 0};
        _stR = {EYE_R_X, EYE_Y, 80, 8, 0};
        break;

    case CAT_CURIOUS:
        _stL = {(int16_t)(EYE_L_X + 5), EYE_Y, 70, 70, 0};
        _stR = {(int16_t)(EYE_R_X + 5), (int16_t)(EYE_Y - 5), 110, 110, 0};
        break;

    case CAT_TALKING: {
        _stL = {EYE_L_X, EYE_Y, EYE_D, EYE_D, 0};
        _stR = {EYE_R_X, EYE_Y, EYE_D, EYE_D, 0};

        lv_obj_set_size(_mouth, 14, 10);
        lv_obj_set_style_radius(_mouth, 5, 0);
        lv_obj_set_pos(_mouth, MOUTH_X - 7, MOUTH_Y - 5);
        lv_obj_remove_flag(_mouth, LV_OBJ_FLAG_HIDDEN);

        lv_anim_t ma;
        lv_anim_init(&ma);
        lv_anim_set_var(&ma, this);
        lv_anim_set_values(&ma, 0, 1000);
        lv_anim_set_duration(&ma, MOUTH_DUR);
        lv_anim_set_reverse_duration(&ma, MOUTH_DUR);
        lv_anim_set_path_cb(&ma, lv_anim_path_ease_in_out);
        lv_anim_set_exec_cb(&ma, _mouthAnimCb);
        lv_anim_set_repeat_count(&ma, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&ma);
        break;
    }

    default: break;
    }

    // Start expression morph animation
    _exprProgress = 0;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, this);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, EXPR_DUR);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, _exprAnimCb);
    lv_anim_start(&a);

    // Mask slide: up to form crescent, down to clear
    int16_t targetDY = (e == CAT_HAPPY) ? MASK_DY_ON : MASK_DY_OFF;
    if (targetDY != _maskDY) {
        if (targetDY < MASK_DY_OFF) {
            lv_obj_remove_flag(_maskL, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(_maskR, LV_OBJ_FLAG_HIDDEN);
        }
        lv_anim_t fa;
        lv_anim_init(&fa);
        lv_anim_set_var(&fa, this);
        lv_anim_set_values(&fa, _maskDY, targetDY);
        lv_anim_set_duration(&fa, EXPR_DUR);
        lv_anim_set_path_cb(&fa, lv_anim_path_ease_in_out);
        lv_anim_set_exec_cb(&fa, _maskAnimCb);
        lv_anim_set_completed_cb(&fa, _maskFadeDoneCb);
        lv_anim_start(&fa);
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

    // Crescent masks for Happy (bg-colored circles above eyes)
    auto makeMask = [&](lv_obj_t *&m) {
        m = lv_obj_create(_scr);
        lv_obj_remove_style_all(m);
        lv_obj_set_style_bg_color(m, COL_BG, 0);
        lv_obj_set_style_bg_opa(m, LV_OPA_COVER, 0);
        lv_obj_remove_flag(m,
            (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
        lv_obj_add_flag(m, LV_OBJ_FLAG_HIDDEN);
    };
    makeMask(_maskL);
    makeMask(_maskR);

    _mouth = lv_obj_create(_scr);
    lv_obj_remove_style_all(_mouth);
    lv_obj_set_style_bg_color(_mouth, COL_MOUTH, 0);
    lv_obj_set_style_bg_opa(_mouth, LV_OPA_COVER, 0);
    lv_obj_remove_flag(_mouth,
        (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_add_flag(_mouth, LV_OBJ_FLAG_HIDDEN);

    // Eyes grow from center on boot — a nice pop-in effect
    _baseL = {EYE_L_X, EYE_Y, 0, 0, 0};
    _baseR = {EYE_R_X, EYE_Y, 0, 0, 0};

    _apply(CAT_NEUTRAL);
}

// ==========================================================
//  Update — gesture handling only (animations are self-driving)
// ==========================================================
void CatFace::update() {
    uint32_t now = lv_tick_get();

    static GESTURE prevG = NONE;
    GESTURE g = touch_data.gesture;

    if (g != NONE && g != prevG) {
        Serial.printf("[CatFace] gesture=%d\n", (int)g);
        if (g == SINGLE_CLICK || g == LONG_PRESS) {
            if (_cur != CAT_HAPPY) _baseExpr = _cur;
            _touchHappyEnd = now + 2000;
            Serial.println("[CatFace] Touch -> Happy");
            _apply(CAT_HAPPY);
        } else if (g == SWIPE_RIGHT || g == SWIPE_DOWN) {
            _touchHappyEnd = 0;
            int ne = ((int)_baseExpr + 1) % CAT_EXPR_COUNT;
            _baseExpr = (CatExpression)ne;
            Serial.printf("[CatFace] Swipe -> %s\n", catExprName(_baseExpr));
            _apply(_baseExpr);
        } else if (g == SWIPE_LEFT || g == SWIPE_UP) {
            _touchHappyEnd = 0;
            int ne = ((int)_baseExpr - 1 + CAT_EXPR_COUNT) % CAT_EXPR_COUNT;
            _baseExpr = (CatExpression)ne;
            Serial.printf("[CatFace] Swipe -> %s\n", catExprName(_baseExpr));
            _apply(_baseExpr);
        }
    }
    prevG = g;

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
