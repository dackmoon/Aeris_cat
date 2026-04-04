#pragma once
#include <lvgl.h>

enum CatExpression {
    CAT_NEUTRAL = 0,
    CAT_HAPPY,
    CAT_SAD,
    CAT_ANGRY,
    CAT_SLEEPY,
    CAT_CURIOUS,
    CAT_TALKING,
    CAT_EXPR_COUNT
};

const char *catExprName(CatExpression e);

class CatFace {
public:
    void create(lv_obj_t *parent);
    void setExpression(CatExpression expr);
    CatExpression getExpression() const { return _cur; }
    void update();

private:
    lv_obj_t *_scr   = nullptr;
    lv_obj_t *_eyeL  = nullptr;
    lv_obj_t *_eyeR  = nullptr;
    lv_obj_t *_maskL = nullptr;    // Happy crescent mask (bg-colored)
    lv_obj_t *_maskR = nullptr;
    lv_obj_t *_mouth = nullptr;

    struct EyeState { int16_t cx, cy, w, h, rot; };

    EyeState _stL{}, _stR{};           // Target expression eye state
    EyeState _fromL{}, _fromR{};       // Expression anim start state
    EyeState _baseL{}, _baseR{};       // Current interpolated base (before blink/look)

    int32_t _exprProgress = 1000;      // 0 = from, 1000 = target

    CatExpression _cur      = CAT_NEUTRAL;
    CatExpression _baseExpr = CAT_NEUTRAL;

    int32_t  _blinkFactor = 0;         // 0 = open, 1000 = closed

    int16_t  _lookDX = 0, _lookDY = 0;
    int16_t  _lookTgtDX = 0, _lookTgtDY = 0;

    int16_t  _maskDY = 82;             // mask Y offset (82 = off-screen, 20 = crescent)

    uint32_t _touchHappyEnd = 0;

    void _placeEye(lv_obj_t *eye, const EyeState &st,
                   int16_t dx = 0, int16_t dy = 0);
    void _apply(CatExpression e);
    void _render();
    void _scheduleBlink();
    void _scheduleLook();

    static EyeState _lerpState(const EyeState &a, const EyeState &b, int32_t t);

    static void _exprAnimCb(void *var, int32_t v);
    static void _blinkAnimCb(void *var, int32_t v);
    static void _blinkDoneCb(lv_anim_t *a);
    static void _lookAnimCb(void *var, int32_t v);
    static void _lookDoneCb(lv_anim_t *a);
    static void _mouthAnimCb(void *var, int32_t v);
    static void _maskAnimCb(void *var, int32_t v);
    static void _maskFadeDoneCb(lv_anim_t *a);
};

extern CatFace catFace;
