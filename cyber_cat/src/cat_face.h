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

// How the cat feels — sets the eye shape; layered on top of any activity.
enum CatMood : uint8_t {
    MOOD_NEUTRAL = 0,
    MOOD_HAPPY,
    MOOD_CURIOUS,
    MOOD_CONFUSED,
    MOOD_SURPRISED,
    MOOD_COUNT
};

// Optional decoration layers (bit mask).
enum : uint8_t {
    DECOR_NONE   = 0,
    DECOR_PUPILS = 1 << 0,
    DECOR_BLUSH  = 1 << 1,
    DECOR_MOUTH  = 1 << 2,
    DECOR_ALL    = DECOR_PUPILS | DECOR_BLUSH | DECOR_MOUTH,
};

const char *catActivityName(CatActivity a);
const char *catMoodName(CatMood m);

// Procedural cat face. Every eye parameter follows its target through a damped
// spring, so any change of activity/mood mid-animation stays continuous.
// All methods must be called with the LVGL lock held (LVGL task / update cb).
class CatFace {
public:
    struct Spring {
        float x = 0, v = 0, target = 0;
        float freq = 4.0f, damp = 0.8f;
        void step(float dt);
        void snap(float value) { x = target = value; v = 0; }
    };

    enum Param : uint8_t { P_DX, P_DY, P_W, P_H, P_LID, P_TILT, P_BOT, P_COUNT };

    // Eye pose relative to the eye's home position. lid: upper-lid cover 0..1;
    // tilt: + lowers the inner corner of the upper lid; bot: lower-lid push 0..1.
    struct Pose { float dx, dy, w, h, lid, tilt, bot; };

    void create(lv_obj_t *parent);   // also starts the internal 16ms tick timer

    void setActivity(CatActivity a);
    void setMood(CatMood m);
    void pulseMood(CatMood m, uint32_t durationMs);
    void setDrowsiness(float d);              // 0..1, only applied while idle
    void setSpeechLevel(uint16_t rms);        // speaker output level while talking
    void setListenLevel(uint16_t rms);        // microphone level while listening
    void setTouch(bool pressed, int16_t x, int16_t y);
    void setDecor(uint8_t mask);
    // Tooling (simulator snapshots): disable random blinks and glances.
    void setAutoBehaviours(bool on) { _auto = on; }

    CatActivity activity() const { return _activity; }
    CatMood     mood() const;                 // effective mood (pulse wins)
    uint8_t     decor() const { return _decor; }

private:
    struct EyeGeom {
        int16_t x1, y1, x2, y2;     // eye white bounds
        int16_t lidOuter, lidInner; // upper-lid edge y at outer / inner side
        int16_t botCy, botR;        // lower-lid circle (botR = 0: none)
        int16_t px, py, pr;         // pupil (pr = 0: none)
        bool operator!=(const EyeGeom &o) const;
    };
    struct DecorGeom {
        int16_t blushDx, blushDy;
        uint8_t blushOpa;
        int16_t mouthX, mouthY, mouthR, mouthOpen;
        bool operator!=(const DecorGeom &o) const;
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
    float       _speechEnv = 0, _listenEnv = 0;
    bool        _touch = false;
    int16_t     _touchX = 0, _touchY = 0;
    uint8_t     _decor = DECOR_NONE;
    bool        _auto = true;

    Spring _eye[2][P_COUNT];
    Spring _gazeX, _gazeY;
    Spring _blushOpa, _mouthOpen;

    float    _gazeTgtX = 0, _gazeTgtY = 0;
    uint32_t _nextGazeMs = 0;
    bool     _thinkLeft = false;

    uint32_t _blinkStartMs = 0;
    uint16_t _blinkCloseMs = 0, _blinkOpenMs = 0;
    uint8_t  _blinkOffsetMs = 0;
    bool     _blinkTwice = false;
    uint32_t _nextBlinkMs = 0;

    EyeGeom   _geom[2]{};
    DecorGeom _decorGeom{};

    static void _timerCb(lv_timer_t *t);
    static void _drawCb(lv_event_t *e);
    static void _drawEye(lv_layer_t *layer, int eye, const EyeGeom &g);

    void _tick();
    void _targets(uint32_t now, Pose out[2]) const;
    void _scheduleGaze(uint32_t now);
    void _scheduleBlink(uint32_t now, bool soon);
    float _blinkAmount(uint32_t now, int eye) const;
    void _computeGeom(uint32_t now, EyeGeom out[2], DecorGeom &decor);
    void _invalidate(const EyeGeom &g);
    void _invalidateDecor(const DecorGeom &d);
    void _draw(lv_layer_t *layer) const;
};

extern CatFace catFace;
