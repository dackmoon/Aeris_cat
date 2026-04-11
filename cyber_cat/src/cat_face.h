#pragma once
#include <lvgl.h>

enum CatExpression {
    CAT_NEUTRAL = 0,
    CAT_HAPPY,
    CAT_LISTENING,   // recording — large asymmetric eyes, subtle pulse
    CAT_THINKING,    // waiting for AI — narrowed tilted eyes, slow gaze
    CAT_SLEEPY,
    CAT_TALKING,
    CAT_EXPR_COUNT
};

const char *catExprName(CatExpression e);

typedef void (*LongPressCb)();
typedef bool (*TapCb)();     // return true = tap consumed (skip default Happy)

class CatFace {
public:
    // Eye geometry — also used by file-scope talk-stage table in cat_face.cpp
    struct EyeState { int16_t cx, cy, w, h, rot; };

    void create(lv_obj_t *parent);
    void setExpression(CatExpression expr);
    CatExpression getExpression() const { return _cur; }
    uint32_t lastInteractMs() const { return _lastTouchMs; }
    void update();

    // Drowsy transition: slowly shrinks eye height toward 20px over 3000ms.
    // Called at the 90s idle mark; setExpression(CAT_SLEEPY) follows at 120s.
    // No-op if already SLEEPY or in an active dialog expression.
    void startDrowsy();

    void onLongPress(LongPressCb cb) { _longPressCb = cb; }
    void onTap(TapCb cb)             { _tapCb = cb; }

private:
    lv_obj_t *_scr  = nullptr;
    lv_obj_t *_eyeL = nullptr;
    lv_obj_t *_eyeR = nullptr;

    EyeState _stL{}, _stR{};           // Target expression eye state
    EyeState _fromL{}, _fromR{};       // Expression anim start state
    EyeState _baseL{}, _baseR{};       // Current interpolated base (before blink/look)

    int32_t _exprProgress = 1000;      // 0 = from, 1000 = target

    CatExpression _cur      = CAT_NEUTRAL;
    CatExpression _baseExpr = CAT_NEUTRAL;

    uint8_t  _animEpoch   = 0;         // incremented on each _apply(); guards stale done-callbacks
    int32_t  _blinkFactor  = 0;        // 0 = open, 1000 = closed
    int32_t  _breathFactor = 0;        // 0 = -2px, 1000 = +2px (Neutral breathing rhythm)
    int32_t  _drowsyH      = -1;       // current eye h override during drowsy transition (-1 = inactive)

    int16_t  _lookDX = 0, _lookDY = 0;
    int16_t  _lookTgtDX = 0, _lookTgtDY = 0;

    // Talking: two reusable staging buffers (LVGL copies them on lv_anim_start)
    lv_anim_t _talkAnimL{};
    lv_anim_t _talkAnimR{};
    int8_t    _talkStage = -1;         // -1 = idle, 0-3 = current loop stage

    uint32_t _lastTouchMs   = 0;      // millis() of last processed gesture
    uint8_t  _prevGesture   = 0;      // GESTURE enum value (0 = NONE)
    uint32_t _touchHappyEnd = 0;
    LongPressCb _longPressCb = nullptr;
    TapCb       _tapCb       = nullptr;

    void _placeEye(lv_obj_t *eye, const EyeState &st,
                   int16_t dx = 0, int16_t dy = 0);
    void _apply(CatExpression e);
    void _render();
    void _scheduleBlink();
    void _scheduleLook();
    void _scheduleBreath();

    void _startTalking(uint32_t initialDelay = 0);
    void _stopTalking();
    void _startTalkStage(int stage, uint32_t delay = 0);

    static EyeState _lerpState(const EyeState &a, const EyeState &b, int32_t t);

    static void _exprAnimCb(void *var, int32_t v);
    static void _blinkAnimCb(void *var, int32_t v);
    static void _blinkDoneCb(lv_anim_t *a);
    static void _lookAnimCb(void *var, int32_t v);
    static void _lookDoneCb(lv_anim_t *a);
    static void _breathAnimCb(void *var, int32_t v);
    static void _drowsyAnimCb(void *var, int32_t v);
    static void _talkExecCbL(void *var, int32_t t);
    static void _talkExecCbR(void *var, int32_t t);
    static void _talkDoneCb(lv_anim_t *a);
};

extern CatFace catFace;
