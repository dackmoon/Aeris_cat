# lvgl_sim — Cyber Cat 桌面模拟器

## 项目定位

把 ESP32-S3 上运行的 `cat_face.cpp` 直接搬到桌面运行，用于表情参数调试。  
改完参数 → 桌面验证 → 再烧录到设备，省去每次烧录等待。

---

## 构建 & 运行

```bash
cd /Users/weizhong.wang/Workspace/ESP-S3/lvgl_sim

# 首次配置（只需一次）
cmake -B build -G "Ninja Multi-Config" \
  -DLV_BUILD_CONF_PATH=/Users/weizhong.wang/Workspace/ESP-S3/lvgl/lv_conf.h

# 每次改代码后重新编译
cmake --build build --config Release

# 启动模拟器
./build/Release/lvgl_sim
```

---

## 文件结构

```
lvgl_sim/
├── main.cpp                   # 模拟器入口：SDL2 窗口 + 键盘控制 + cat face
├── CMakeLists.txt             # 构建配置
└── mock/
    ├── Arduino.h              # 桌面 stub：millis() lv_tick_get()，random()，Serial → printf
    └── Touch_CST816.h         # 桌面 stub：GESTURE 枚举 + touch_data 全局变量

# 直接引用（不复制）：
../lvgl/                       # LVGL 源码库（v9.5+，master 分支）
../cyber_cat/src/cat_face.cpp  # 真实固件代码，零修改直接编译
../cyber_cat/src/cat_face.h
```

---

## 键盘操作

| 键 | 效果 |
|----|------|
| `1` | CAT_NEUTRAL（呼吸律动） |
| `2` | CAT_HAPPY（宽扁眯眼） |
| `3` | CAT_LISTENING（放大不对称） |
| `4` | CAT_THINKING（仰视外移） |
| `5` | CAT_SLEEPY（细线） |
| `6` | CAT_TALKING（律动循环） |
| `Space` | 模拟点击手势（→ Happy 2s） |
| `H` | 模拟长按手势（触发 onLongPress） |
| `ESC` | 退出 |

---

## 架构说明

`cat_face.cpp` 依赖两个硬件头文件，在模拟器里被 mock 替换：

| 原始头文件 | Mock 替换 | 说明 |
|-----------|-----------|------|
| `<Arduino.h>` | `mock/Arduino.h` | `millis()` → `lv_tick_get()`，`random()` → `rand()`，`Serial` → `printf` |
| `Touch_CST816.h` | `mock/Touch_CST816.h` | 同名枚举 + `touch_data` 全局，由 `main.cpp` 键盘事件写入 |

CMake include 优先级：`mock/` > `cyber_cat/src/` > `lvgl/`，确保 mock 头文件覆盖原始头文件。

---

## lv_conf.h 关键改动（相对 LVGL 默认模板）

| 配置项 | 值 | 原因 |
|--------|----|------|
| `#if 0` guard | 改为 `#if 1` | 启用配置文件 |
| `LV_USE_SDL` | `1` | 启用 SDL2 显示驱动 |
| `LV_SDL_INCLUDE_PATH` | `<SDL.h>` | Homebrew SDL2 include 路径为 `/opt/homebrew/include/SDL2/`，用 `<SDL.h>` 避免双层目录问题 |
| `LV_USE_DEMO_WIDGETS` | `1` | 启用 widget demo（可选） |

---

## 依赖

```
brew install sdl2 cmake ninja
```

- **LVGL 版本**：`../lvgl` master（v9.5+）；固件用 9.3，API 兼容
- **macOS**：Apple Silicon，Homebrew SDL2 路径 `/opt/homebrew`

---

## 与固件的同步

`cat_face.cpp / cat_face.h` 通过路径直接引用，**不需要手动同步**。  
固件改完表情参数后，模拟器直接 `cmake --build` 即可验证。

唯一需要同步的是 `cat_face.h` 里若新增了对其他 ESP32 头文件的依赖，需在 `mock/` 里补一个对应的桌面 stub。

---

## 常见问题

| 现象 | 原因 | 修复 |
|------|------|------|
| `SDL.h not found` | `LV_SDL_INCLUDE_PATH` 仍为 `<SDL2/SDL.h>` | 改为 `<SDL.h>`（见上表） |
| `lv_conf.h not found` | cmake 未指定 `-DLV_BUILD_CONF_PATH` | 加参数重新 `cmake -B build` |
| 窗口弹出但立刻退出 | SDL2 事件循环问题 | 检查 `handleEvents()` 返回值 |
| 表情切换无反应 | `touch_data.gesture` 只保持一帧 | 正常：每帧清零，符合 `_prevGesture` 去重逻辑 |
