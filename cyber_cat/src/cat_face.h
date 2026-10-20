#pragma once
#include <lvgl.h>
#include <stdint.h>

// What the cat is doing — driven by the dialog state machine in main.cpp.
enum CatActivity : uint8_t {
    ACT_IDLE = 0,
    ACT_LISTENING,
    ACT_THINKING,
    ACT_TALKING,
    ACT_SLEEPING,
    ACT_COUNT
};

// How the cat feels — sets the eye shape & cyber color; layered on any activity.
enum CatMood : uint8_t {
    MOOD_NEUTRAL = 0,
    MOOD_HAPPY,
    MOOD_CURIOUS,
    MOOD_CONFUSED,
    MOOD_SURPRISED,
    MOOD_LOVE,       // bashful / affectionate (pink cyber glow + blush + sway)
    MOOD_SAD,        // wronged / sad (drooping outer corners + shimmering tear)
    MOOD_ANGRY,      // tsundere / angry (sharp inner slant + coral-red + tremor)
    MOOD_WINK,       // playful wink / smug (> o)
    MOOD_COUNT
};

// Optional decoration layers (bit mask).
enum : uint8_t {
    DECOR_NONE      = 0,
    DECOR_HIGHLIGHT = 1 << 0,   // secondary-motion cyber specular highlight inside eyes
    DECOR_BLUSH     = 1 << 1,   // cyber slash blush "///" under the eyes
    DECOR_MOUTH     = 1 << 2,   // small cyber mouth
    DECOR_PUPILS    = DECOR_HIGHLIGHT, // alias for backward compatibility
    DECOR_ALL       = DECOR_HIGHLIGHT | DECOR_BLUSH | DECOR_MOUTH,
};

const char *catActivityName(CatActivity a);
const char *catMoodName(CatMood m);

// Cozmo / Vector style procedural cyber face with scanline SDF rasterizer.
// All methods must be called with the LVGL lock held (LVGL task / update cb).
class CatFace {
public:
    struct Spring {
        float x = 0, v = 0, target = 0;
        float freq = 4.5f, damp = 0.75f;
        void step(float dt);
        void snap(float value) { x = target = value; v = 0; }
    };

    enum Param : uint8_t {
        P_DX = 0,
        P_DY,
        P_W,
        P_H,
        P_CORNER,    // corner roundness 0.15 (boxy) .. 0.48 (capsule/circle)
        P_TOP_LID,   // upper lid cover 0..1
        P_TOP_TILT,  // + lowers inner corner (\ /), - lowers outer corner (/ \)
        P_BOT_LID,   // lower lid cover 0..1
        P_BEND,      // centerline parabolic bend in px: + arches up (∩), - bows down (︶)
        P_COUNT
    };

    struct Pose {
        float dx, dy, w, h, corner, topLid, topTilt, botLid, bend;
    };

    void create(lv_obj_t *parent);

    void setActivity(CatActivity a);
    void setMood(CatMood m);
    void pulseMood(CatMood m, uint32_t durationMs);
    void setDrowsiness(float d);              // 0..1, only applied while idle
    void setSpeechLevel(uint16_t rms);        // speaker output level while talking
    void setListenLevel(uint16_t rms);        // microphone level while listening
    void setTouch(bool pressed, int16_t x, int16_t y);
    void setDecor(uint8_t mask);
    // Tooling (simulator snapshots): disable random blinks, glances, and idle clips.
    void setAutoBehaviours(bool on) { _auto = on; }

    CatActivity activity() const { return _activity; }
    CatMood     mood() const;                 // effective mood (pulse wins)
    uint8_t     decor() const { return _decor; }

private:
    // Autonomous micro-behaviour clips in ACT_IDLE
    enum IdleClip : uint8_t {
        CLIP_NONE = 0,
        CLIP_LOOK_AROUND,   // glance side -> pause -> opposite side -> center
        CLIP_CURIOUS_PEEK,  // single-brow squint + head tilt
        CLIP_HAPPY_SQUINT,  // contented slow-blink smile
        CLIP_ALERT_PERK,    // quick ear-perk widen + upward dart
        CLIP_DROWSY_NOD,    // droop eyelids -> jerk awake -> droop again
    };

    struct EyeRender {
        float    cx, cy, hw, hh, corner;
        float    topLid, topTilt, botLid, bend, skewX, skewY;
        float    hlX, hlY;          // secondary-motion highlight center
        uint8_t  r, g, b;           // eye cyber phosphor color
        bool     tear, heart;
        int16_t  bx1, by1, bx2, by2; // integer bounding box on screen
    };

    lv_obj_t   *_obj = nullptr;
    lv_timer_t *_timer = nullptr;
    uint32_t    _lastTickMs = 0;

    CatActivity _activity = ACT_IDLE;
    uint32_t    _activitySinceMs = 0;
    CatMood     _mood = MOOD_NEUTRAL;
    CatMood     _pulseMood = MOOD_NEUTRAL;
    uint32_t    _pulseUntilMs = 0;
    uint32_t    _wakeMs = 0;
    float       _drowsy = 0;
    uint16_t    _speechRms = 0, _listenRms = 0;
    float       _speechFast = 0, _speechSlow = 0, _listenEnv = 0;
    bool        _touch = false;
    int16_t     _touchX = 0, _touchY = 0;
    uint8_t     _decor = DECOR_HIGHLIGHT | DECOR_BLUSH;
    bool        _auto = true;

    Spring _eye[2][P_COUNT];
    Spring _gazeX, _gazeY;
    Spring _hlX, _hlY;              // highlight lags behind gaze (secondary motion)
    Spring _colR, _colG, _colB;     // smooth phosphor color transitions
    Spring _blushOpa, _mouthOpen;

    float    _gazeTgtX = 0, _gazeTgtY = 0;
    uint32_t _nextGazeMs = 0;
    bool     _thinkLeft = false;

    IdleClip _clip = CLIP_NONE;
    uint32_t _clipStartMs = 0;
    uint32_t _nextClipMs = 0;

    uint32_t _blinkStartMs = 0;
    uint16_t _blinkCloseMs = 0, _blinkOpenMs = 0;
    uint8_t  _blinkOffsetMs = 0;
    float    _blinkDepth = 1.0f;    // 1.0 = full blink, 0.4 = saccadic half-blink
    bool     _blinkTwice = false;
    uint32_t _nextBlinkMs = 0;

    EyeRender _cur[2]{};
    lv_area_t _prevEyeBox[2]{};
    lv_area_t _prevDecorBox{};
    bool      _hasPrevBox = false;

    static void _timerCb(lv_timer_t *t);
    static void _drawCb(lv_event_t *e);

    void  _tick();
    void  _targets(uint32_t now, Pose out[2], uint8_t rgb[3]) const;
    void  _applyIdleClip(uint32_t now, Pose out[2], float &gx, float &gy);
    void  _scheduleGaze(uint32_t now);
    void  _scheduleBlink(uint32_t now, bool soon, float depth = 1.0f);
    float _blinkAmount(uint32_t now, int eye) const;
    void  _buildRender(uint32_t now, EyeRender out[2]);
    void  _draw(lv_layer_t *layer) const;
    static void _rasterizeEye(lv_layer_t *layer, int eyeIdx, const EyeRender &e);
    void  _rasterizeDecor(lv_layer_t *layer, uint32_t now) const;
};

extern CatFace catFace;
