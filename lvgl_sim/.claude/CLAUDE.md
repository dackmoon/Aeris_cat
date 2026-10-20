# lvgl_sim — Cyber Cat 桌面模拟器

## 项目定位

把 ESP32-S3 上运行的 `cat_face.cpp` 直接搬到桌面运行，用于表情参数调试。  
改完参数 → 桌面验证 → 再烧录到设备，省去每次烧录等待。

---

## 构建 & 运行

```bash
# 首次：拉取 LVGL 子模块（与固件同为 v9.3.0）+ 安装依赖
cd Aeris_cat && git submodule update --init lvgl
brew install sdl2 cmake ninja

cd lvgl_sim
# 首次配置（只需一次）；lv_conf.h 由 CMakeLists.txt 指向本目录
cmake -B build -G "Ninja Multi-Config"

# 每次改代码后重新编译
cmake --build build --config Release

# 启动模拟器
./build/Release/lvgl_sim

# 截图模式：渲染 12 种状态拼成 grid.ppm 后退出（可选第二个参数为装饰掩码，默认 3=高光+腮红）
./build/Release/lvgl_sim --shots /tmp/catshots [decor]
sips -s format png /tmp/catshots/grid.ppm --out grid.png

# 装饰层对比：高光+腮红 / 无 / 仅高光 / 全部，各 5 种状态 → decor.ppm
./build/Release/lvgl_sim --decor-shots /tmp/catshots
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
../lvgl/                       # LVGL 源码库（git 子模块，锁定 v9.3.0，与固件一致）
../cyber_cat/src/cat_face.cpp  # 真实固件代码，零修改直接编译
../cyber_cat/src/cat_face.h
```

---

## 键盘操作

| 键 | 效果 |
|----|------|
| `1`–`5` | Activity：Idle / Listening / Thinking / Talking / Sleeping |
| `Q W E R T` | 基础 Mood：Neutral / Happy / Curious / Confused / Surprised |
| `A S F G` | 新增 Mood：`A` Love（害羞爱心）/ `S` Sad（委屈泪光）/ `F` Angry（傲娇生气）/ `G` Wink（俏皮眨眼） |
| `Y` / `U` | 闪现 Surprised 0.4s / Confused 1.5s |
| `D` | 装饰层循环：高光+腮红（默认）→ 无装饰 → 仅高光 → 全部（含小嘴） |
| `V` | 开关合成语音电平（驱动 Listening 放大、Talking 音节拍点弹跳） |
| `Z` | 开关犯困渐变（6s 从清醒到最困，可触发犯困点头猛醒小剧场） |
| 鼠标按住 | 模拟手指按在屏幕上（眼睛体积守恒压扁、视线跟随手指） |
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
| `LV_USE_STDLIB_MALLOC` | `LV_STDLIB_CLIB` | 用系统 malloc；内置 64KB 池放不下整屏截图缓冲 |
| `LV_USE_SNAPSHOT` | `1` | `--shots` 截图模式需要 |

---

## 依赖

```
brew install sdl2 cmake ninja
```

- **LVGL 版本**：`../lvgl` 子模块锁定 v9.3.0，与固件 `lvgl/lvgl@~9.3.0` 一致
- **CMake**：LVGL 9.3 的 CMake 不会给 lvgl 目标加 SDL 头文件路径，`CMakeLists.txt` 中手动补了 `target_include_directories(lvgl PUBLIC ${SDL2_INCLUDE_DIRS})`
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
| 截图模式卡住 / 崩溃 | 截图耗时让帧间 dt 变大 | `Spring::step` 已用隐式积分 + 子步，若改回显式积分会复现 |
