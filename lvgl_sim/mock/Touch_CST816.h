#pragma once
#include <stdint.h>

// Mirror the real Touch_CST816.h exactly — cat_face.cpp depends on these values
enum GESTURE {
    NONE         = 0x00,
    SINGLE_CLICK = 0x05,
    LONG_PRESS   = 0x0C,
};

struct CST816_Touch {
    uint16_t x = 0;
    uint16_t y = 0;
    GESTURE  gesture = NONE;
};

// Defined in main.cpp; cat_face.cpp reads this each frame via update()
extern CST816_Touch touch_data;
