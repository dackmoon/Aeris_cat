# 1.7 LVGL 图形界面基础

> 最后更新：2026-04-11  
> 适用版本：Cyber Cat Phase 4/5

---

## 本章目标

完成本章后，你将：
- [ ] 理解 LVGL 的架构和工作原理
- [ ] 掌握 LVGL 在 ESP32 上的移植方法
- [ ] 能够创建和样式化 LVGL 对象
- [ ] 理解 LVGL 动画系统和事件回调

---

## 1.7.1 LVGL 架构与移植

### LVGL 简介

**LVGL 是什么？** LVGL（Light and Versatile Graphics Library）是一个开源的嵌入式图形库，特点：
- 轻量级（最小配置仅需几 KB RAM）
- 跨平台（支持 MCU、MPU、PC）
- 丰富的控件（按钮、图表、列表等）
- 强大的动画和过渡效果
- 完全免费（MIT 许可证）

**本项目配置：**
- 版本：LVGL v9.3.0
- 分辨率：360x360（圆形屏幕）
- 刷新率：60fps
- 内存：64KB PSRAM

### LVGL 架构

```
┌─────────────────────────────────────┐
│         应用层 (你的代码)            │
├─────────────────────────────────────┤
│         LVGL API 层                  │
│  - 对象创建 (lv_obj_create)         │
│  - 样式设置 (lv_obj_set_style)      │
│  - 动画系统 (lv_anim_t)             │
├─────────────────────────────────────┤
│         显示驱动适配层               │
│  - lv_disp_draw_buf_init()          │
│  - lv_disp_drv_init()               │
│  - flush_cb 回调                    │
├─────────────────────────────────────┤
│         硬件层 (SPI QSPI)            │
│  - 屏幕驱动 (ST77916)               │
│  - DMA 传输                         │
└─────────────────────────────────────┘
```

### LVGL 移植步骤

**步骤 1：安装 LVGL 库**

```ini
; platformio.ini
[env:esp32s3]
lib_deps =
    lvgl/lvgl@~9.3.0
```

**步骤 2：配置 lv_conf.h**

```cpp
// lv_conf.h（本项目关键配置）
#define LV_COLOR_DEPTH 16           // 16 位色深
#define LV_DISP_DEF_REFR_PERIOD 16  // 60fps 刷新
#define LV_MEM_SIZE (64 * 1024)     // 64KB heap
#define LV_USE_TICK 1               // 启用时间戳
#define LV_CONF_INCLUDE_SIMPLE      // 简单 include 方式

// 启用/禁用模块
#define LV_USE_BTN 1
#define LV_USE_LABEL 1
#define LV_USE_IMG 1
#define LV_USE_ANIMATION 1
```

**步骤 3：初始化显示驱动**

```cpp
#include <lvgl.h>
#include "LVGL_Driver.h"

// 显示缓冲区（1/4 屏幕）
#define DRAW_BUF_SIZE (360 * 360 / 4 * 2)
static lv_color_t* draw_buf;

// 显示 flush 回调（将渲染结果发送到屏幕）
void my_flush_cb(lv_disp_drv_t* disp, const lv_area_t* area, uint8_t* px_map) {
    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;
    
    // 调用屏幕驱动发送数据
    display_write(area->x1, area->y1, w, h, px_map);
    
    // 通知 LVGL 完成
    lv_disp_flush_ready(disp);
}

// 初始化 LVGL
void lvgl_init() {
    lv_init();
    
    // 分配显示缓冲区（PSRAM）
    draw_buf = (lv_color_t*)ps_malloc(DRAW_BUF_SIZE);
    
    // 初始化显示驱动
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 360;
    disp_drv.ver_res = 360;
    disp_drv.flush_cb = my_flush_cb;
    disp_drv.draw_buf = draw_buf;
    lv_disp_drv_register(&disp_drv);
    
    // 创建 LVGL 任务
    xTaskCreatePinnedToCore(
        lvgl_task_func,
        "LVGL",
        4096,
        NULL,
        8,
        NULL,
        0  // Core 0
    );
}

// LVGL 任务函数
void lvgl_task_func(void* parameter) {
    while (true) {
        lv_timer_handler();
        vTaskDelay(16 / portTICK_PERIOD_MS);
    }
}
```

**步骤 4：定时调用 lv_timer_handler()**

```cpp
// 方式一：FreeRTOS 任务（推荐）
void lvgl_task_func(void* parameter) {
    while (true) {
        lv_timer_handler();  // 处理动画、刷新等
        vTaskDelay(16 / portTICK_PERIOD_MS);  // 60fps
    }
}

// 方式二：在 loop() 中调用
void loop() {
    lv_timer_handler();
    delay(16);
}
```

---

## 1.7.2 对象与样式

### 对象基础

**什么是 LVGL 对象？** LVGL 中的一切都是对象（widget），如按钮、标签、图像等。

```cpp
// 创建对象
lv_obj_t* myButton = lv_button_create(lv_screen_active());

// 设置位置
lv_obj_set_pos(myButton, 50, 100);

// 设置大小
lv_obj_set_size(myButton, 100, 50);

// 设置样式
lv_obj_set_style_bg_color(myButton, lv_color_hex(0xFF0000), 0);

// 删除对象
lv_obj_del(myButton);
```

### 常用对象类型

**标签（Label）：**

```cpp
// 创建标签
lv_obj_t* label = lv_label_create(lv_screen_active());
lv_label_set_text(label, "Hello, LVGL!");
lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

// 设置长文本自动换行
lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
lv_obj_set_width(label, 200);

// 设置文本格式
lv_label_set_text_fmt(label, "温度：%d°C", temperature);
```

**按钮（Button）：**

```cpp
// 创建按钮
lv_obj_t* btn = lv_button_create(lv_screen_active());
lv_obj_set_size(btn, 120, 50);
lv_obj_center(btn);

// 添加事件回调
lv_obj_add_event_cb(btn, button_click_cb, LV_EVENT_CLICKED, NULL);

// 按钮内添加标签
lv_obj_t* label = lv_label_create(btn);
lv_label_set_text(label, "点击我");
lv_obj_center(label);
```

**图像（Image）：**

```cpp
// 创建图像（需要先转换图片为 C 数组）
LV_IMG_DECLARE(my_image);  // 声明外部图像
lv_obj_t* img = lv_image_create(lv_screen_active());
lv_image_set_src(img, &my_image);
lv_obj_center(img);
```

**弧形（Arc）- 用于进度条：**

```cpp
// 创建弧形
lv_obj_t* arc = lv_arc_create(lv_screen_active());
lv_arc_set_rotation(arc, 135);      // 起始角度
lv_arc_set_bg_angles(arc, 0, 270);  // 背景弧度
lv_arc_set_value(arc, 50);          // 当前值
lv_obj_center(arc);
```

### 样式（Style）

**创建自定义样式：**

```cpp
// 定义样式
static lv_style_t myStyle;
lv_style_init(&myStyle);

// 设置背景色
lv_style_set_bg_color(&myStyle, lv_color_hex(0x00FF00));

// 设置边框
lv_style_set_border_color(&myStyle, lv_color_hex(0xFFFFFF));
lv_style_set_border_width(&myStyle, 2);

// 设置圆角
lv_style_set_radius(&myStyle, 10);

// 设置阴影
lv_style_set_shadow_color(&myStyle, lv_color_hex(0x000000));
lv_style_set_shadow_width(&myStyle, 10);
lv_style_set_shadow_ofs_y(&myStyle, 5);

// 应用样式到对象
lv_obj_add_style(myButton, &myStyle, 0);
```

**状态样式（不同状态有不同外观）：**

```cpp
static lv_style_t style_normal;
static lv_style_t style_pressed;

lv_style_init(&style_normal);
lv_style_set_bg_color(&style_normal, lv_color_hex(0x00FF00));

lv_style_init(&style_pressed);
lv_style_set_bg_color(&style_pressed, lv_color_hex(0x00AA00));

// 正常状态用 style_normal，按下状态用 style_pressed
lv_obj_add_style(btn, &style_normal, 0);
lv_obj_add_style(btn, &style_pressed, LV_STATE_PRESSED);
```

### 布局（Layout）

**Flex 布局：**

```cpp
lv_obj_t* container = lv_obj_create(lv_screen_active());
lv_obj_set_size(container, 300, 200);
lv_obj_center(container);

// 启用 Flex 布局
lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
lv_obj_set_flex_align(container, LV_FLEX_ALIGN_SPACE_EVENLY,  // 主轴
                      LV_FLEX_ALIGN_CENTER,                     // 交叉轴
                      LV_FLEX_ALIGN_CENTER);                    // 子项

// 在容器内创建按钮
for (int i = 0; i < 5; i++) {
    lv_obj_t* btn = lv_button_create(container);
    lv_obj_set_size(btn, 50, 50);
}
```

**Grid 布局：**

```cpp
lv_obj_t* container = lv_obj_create(lv_screen_active());

// 定义网格列（3 列，每列 100px）
static const int32_t col_dsc[] = {100, 100, 100, LV_GRID_TEMPLATE_LAST};
// 定义网格行（2 行，每行 50px）
static const int32_t row_dsc[] = {50, 50, LV_GRID_TEMPLATE_LAST};

lv_obj_set_grid_dsc_array(container, col_dsc, row_dsc);

// 在网格中创建对象（第 1 列第 0 行）
lv_obj_t* obj = lv_obj_create(container);
lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_CENTER, 0, 1,
                     LV_GRID_ALIGN_CENTER, 0, 1);
```

---

## 1.7.3 动画系统

### 基础动画

```cpp
// 创建动画
lv_anim_t a;
lv_anim_init(&a);

// 设置要动画的对象
lv_anim_set_var(&a, myButton);

// 设置动画值（从 50 到 200）
lv_anim_set_values(&a, 50, 200);

// 设置动画时长（1000ms）
lv_anim_set_duration(&a, 1000);

// 设置缓动曲线
lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);

// 设置执行回调（每一帧调用）
lv_anim_set_exec_cb(&a, set_width_callback);

// 设置完成回调
lv_anim_set_completed_cb(&a, animation_done_callback);

// 启动动画
lv_anim_start(&a);

// 执行回调函数原型
void set_width_callback(void* var, int32_t value) {
    lv_obj_set_width((lv_obj_t*)var, value);
}
```

### 缓动曲线（Easing）

LVGL 提供多种缓动曲线：

```cpp
// 线性（匀速）
lv_anim_path_linear(a);

// 缓入缓出（慢 - 快 - 慢）
lv_anim_path_ease_in_out(a);

// 缓入（慢 - 快）
lv_anim_path_ease_in(a);

// 缓出（快 - 慢）
lv_anim_path_ease_out(a);

// 过冲（超过目标值再回弹）
lv_anim_path_overshoot(a);

// 弹跳效果
lv_anim_path_bounce(a);
```

### 动画串联和并联

**串联动画（一个接一个）：**

```cpp
// 使用 completed_cb 链式调用
void anim1_done(lv_anim_t* a) {
    lv_anim_t a2;
    lv_anim_init(&a2);
    lv_anim_set_var(&a2, myButton);
    lv_anim_set_values(&a2, 100, 200);  // 从 anim1 的终点开始
    lv_anim_set_duration(&a2, 500);
    lv_anim_set_exec_cb(&a2, set_y_callback);
    lv_anim_start(&a2);
}
```

**并联动画（同时进行）：**

```cpp
// 创建两个独立动画
lv_anim_t a1, a2;

lv_anim_init(&a1);
lv_anim_set_var(&a1, myButton);
lv_anim_set_values(&a1, 50, 200);
lv_anim_set_duration(&a1, 1000);
lv_anim_set_exec_cb(&a1, set_width_callback);
lv_anim_start(&a1);

lv_anim_init(&a2);
lv_anim_set_var(&a2, myButton);
lv_anim_set_values(&a2, 100, 300);
lv_anim_set_duration(&a2, 1000);
lv_anim_set_exec_cb(&a2, set_height_callback);
lv_anim_start(&a2);

// 两个动画同时进行
```

### 项目实际应用 - 猫咪眼睛动画

```cpp
// 眼睛状态结构
struct EyeState {
    int32_t w;      // 宽度
    int32_t h;      // 高度
    int32_t rot;    // 旋转角度
};

// 表情切换动画
void CatFace::_animEye(EyeState target, uint32_t duration) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, _leftEye);
    lv_anim_set_values(&a, _currentEye.w, target.w);
    lv_anim_set_duration(&a, duration);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, _setEyeWidth);
    lv_anim_start(&a);
    
    // 高度动画
    lv_anim_init(&a);
    lv_anim_set_var(&a, _leftEye);
    lv_anim_set_values(&a, _currentEye.h, target.h);
    lv_anim_set_duration(&a, duration);
    lv_anim_set_exec_cb(&a, _setEyeHeight);
    lv_anim_start(&a);
    
    // 旋转动画
    lv_anim_init(&a);
    lv_anim_set_var(&a, _leftEye);
    lv_anim_set_values(&a, _currentEye.rot, target.rot);
    lv_anim_set_duration(&a, duration);
    lv_anim_set_exec_cb(&a, _setEyeRotation);
    lv_anim_start(&a);
}

// 执行回调
void CatFace::_setEyeWidth(void* var, int32_t value) {
    lv_obj_set_width((lv_obj_t*)var, value);
}
```

---

## 1.7.4 事件与回调

### 事件类型

LVGL 支持多种事件：

```cpp
// 常见事件类型
LV_EVENT_CLICKED      // 点击
LV_EVENT_PRESSED      // 按下
LV_EVENT_RELEASED     // 释放
LV_EVENT_PRESS_LOST   // 按压力丢失（滑出）
LV_EVENT_SHORT_CLICKED// 短按
LV_EVENT_LONG_PRESSED // 长按
LV_EVENT_VALUE_CHANGED// 值改变
LV_EVENT_DRAW_POST_END// 绘制完成
```

### 事件回调注册

```cpp
// 简单事件回调
lv_obj_add_event_cb(btn, button_event_cb, LV_EVENT_CLICKED, NULL);

// 带参数的回调
lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, userData);

// 回调函数原型
void button_event_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* target = lv_event_get_target(e);
    
    switch (code) {
        case LV_EVENT_CLICKED:
            Serial.println("按钮被点击");
            break;
    }
}
```

### 手势检测

```cpp
// 启用手势检测
lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
lv_obj_add_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

// 在事件回调中检测手势
void touch_event_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_dir_t dir;
    
    switch (code) {
        case LV_EVENT_GESTURE:
            dir = lv_indev_get_gesture_dir(lv_indev_active());
            switch (dir) {
                case LV_DIR_TOP:
                    Serial.println("上滑");
                    break;
                case LV_DIR_BOTTOM:
                    Serial.println("下滑");
                    break;
                case LV_DIR_LEFT:
                    Serial.println("左滑");
                    break;
                case LV_DIR_RIGHT:
                    Serial.println("右滑");
                    break;
            }
            break;
    }
}
```

### 项目实际应用 - 触摸交互

```cpp
// 触摸事件回调
void CatFace::_tapEventCb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    
    switch (code) {
        case LV_EVENT_CLICKED:
            // 单击：开心表情
            if (_cur != CAT_SLEEPY) {
                _prevExpr = _cur;
                setExpression(CAT_HAPPY);
                _happyTimer = millis() + 2000;  // 2 秒后恢复
            } else {
                setExpression(CAT_NEUTRAL);
            }
            break;
            
        case LV_EVENT_LONG_PRESSED:
            // 长按：触发 AI 对话
            if (_cur != CAT_LISTENING) {
                triggerAIDialog();
            }
            break;
    }
}

// 注册回调
lv_obj_add_event_cb(_eyeContainer, _tapEventCb, 
                    LV_EVENT_ALL, this);
```

---

## 本章小结

本章学习了 LVGL 图形界面基础：
- ✅ LVGL 架构和移植步骤
- ✅ 对象创建和样式设置
- ✅ 动画系统和缓动曲线
- ✅ 事件回调和手势检测

## 下一步

继续学习 **[1.8 Python 后端基础](08-python-基础.md)**，掌握后端开发技能。

---

## 实践练习

1. 创建一个按钮，点击后改变背景颜色
2. 实现一个进度条动画，从 0% 到 100%
3. 创建一个可拖动的滑块，显示当前值
4. 实现手势检测，上滑/下滑切换不同界面

## 常见问题

**Q: LVGL 屏幕闪烁？**  
A: 检查显示缓冲区大小，建议使用至少 1/4 屏幕的缓冲区。确保 flush_cb 正确调用 `lv_disp_flush_ready()`。

**Q: 动画卡顿？**  
A: 确保 `lv_timer_handler()` 每 16ms 调用一次（60fps）。检查 LVGL 任务优先级是否足够高。

**Q: 内存不足？**  
A: 增加 `LV_MEM_SIZE` 或使用 PSRAM。避免创建过多对象，及时删除不用的对象。

**Q: 触摸不响应？**  
A: 确保对象设置了 `LV_OBJ_FLAG_CLICKABLE`，并注册了正确的事件回调。
