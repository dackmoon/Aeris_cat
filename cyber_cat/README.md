# Cyber Cat - AI 陪伴猫

基于微雪 ESP32-S3-Touch-LCD-1.85C 一体板 + 云端 AI，打造一只有表情、能听说、有记忆的 AI 陪伴猫。

ESP32-S3 作为薄客户端负责音频采集/播放和表情显示，所有 AI 推理通过云端完成。

## 硬件：微雪 ESP32-S3-Touch-LCD-1.85C 一体板

不需要任何接线！以下所有外设全部板载集成：

| 模块 | 芯片/型号 | 接口 & 引脚 |
|------|-----------|-------------|
| 主控 | ESP32-S3R8 | 双核 240MHz, 512KB SRAM, 8MB PSRAM, 16MB Flash |
| 屏幕 | ST77916, 1.85寸圆形 360x360, 262K色 | QSPI: SDA0=GPIO46, SDA1=GPIO45, SDA2=GPIO42, SDA3=GPIO41, SCK=GPIO40, CS=GPIO21, TE=GPIO18, RST=EXIO2, BL=GPIO5 |
| 触摸 | CST816 | I2C: SDA=GPIO11, SCL=GPIO10, INT=GPIO4, RST=EXIO1 |
| 喇叭 | PCM5101 音频 DAC | I2S: DIN=GPIO47, LRCK=GPIO38, BCK=GPIO48 |
| 双麦克风 | I2S 数字麦克风 (带回声消除) | I2S: WS=GPIO2, SCK=GPIO15, SD=GPIO39 |
| GPIO 扩展器 | TCA9554PWR | I2C: 控制 LCD_RST, TP_RST, SD_CS 等 |
| 陀螺仪/加速度计 | QMI8658 | I2C: SCL=GPIO10, SDA=GPIO11, INT1=EXIO5, INT2=EXIO4 |
| RTC 时钟 | PCF85063 | I2C: SCL=GPIO10, SDA=GPIO11, INT=GPIO9 |
| SD 卡 | Micro SD 卡槽 | SD: MISO=GPIO16, MOSI=GPIO17, SCK=GPIO14, CS=EXIO3 |
| 电池管理 | 支持 3.7V 锂电池充放电 | MX1.25 2PIN 接口 |
| 其他 | 音量调节旋钮, USB Type-C, RESET/BOOT 按键 | - |

**官方参考代码**：[waveshareteam/ESP32-S3-Touch-LCD-1.85C](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-1.85C)

## 整体架构

```mermaid
graph LR
    subgraph Board ["ESP32-S3-Touch-LCD-1.85C (薄客户端)"]
        MIC["双麦克风 I2S"]
        SPK["PCM5101 喇叭"]
        LCD["ST77916 圆屏 360x360"]
        TOUCH["CST816 触摸"]
        WIFI["WiFi 2.4GHz"]
    end

    subgraph Backend ["后端服务 (Mac/云服务器)"]
        STT["语音识别 STT"]
        LLM["大模型推理 LLM"]
        TTS["语音合成 TTS"]
        MEM["记忆存储"]
        AGENT["Multi-Agent 编排"]
    end

    MIC -->|"录音 PCM"| WIFI
    TOUCH -->|"触摸事件"| WIFI
    WIFI -->|"音频流"| STT
    STT -->|"文字"| AGENT
    AGENT -->|"调用"| LLM
    AGENT -->|"读写"| MEM
    AGENT -->|"回复文字+情绪"| TTS
    TTS -->|"音频流"| WIFI
    WIFI -->|"播放"| SPK
    AGENT -->|"情绪状态"| LCD
```

## 云端 API 方案

| 能力 | 首选方案 | 备选方案 | 说明 |
|------|----------|----------|------|
| LLM（大脑） | 通义千问 qwen-turbo | DeepSeek / GPT-4o | OpenAI 兼容接口，国内直连，速度快 |
| STT（语音转文字） | OpenAI Whisper API | 讯飞 STT | Whisper 中文识别优秀；讯飞国内更快 |
| TTS（文字转语音） | Edge TTS（免费） | 讯飞 TTS | 中文语音自然度好 |

## 开发路线

### Phase 1: 硬件基础 - "让猫睁开眼睛" (已完成 2025-04-04)

**目标**：验证所有硬件能正常工作，跑通微雪官方驱动

- [x] 修改 `platformio.ini`：配置 ESP32-S3R8 参数（PSRAM、Flash、分区表等）
- [x] 连接 WiFi 并在串口打印 IP 地址
- [x] 移植微雪官方 ST77916 + TCA9554PWR 驱动，点亮圆屏
- [x] 集成 LVGL 图形库，在圆屏上显示内容
- [x] 测试 I2S 双麦克风录音（串口打印音量值）
- [x] 测试 PCM5101 I2S 播放（播放一段内置音频）
- [x] 测试 CST816 触摸事件

**完成总结**：

使用 pioarduino 平台（Arduino ESP32 v3.1.3 + ESP-IDF 5.3），从微雪 GitHub 仓库移植全套驱动到 `lib/waveshare/`，集成 LVGL v9.3 图形库。`src/main.cpp` 按顺序初始化 I2C、屏幕、LVGL、WiFi、I2S 音频，创建测试 UI 显示 WiFi 状态、触摸坐标和麦克风音量，启动时播放 beep 提示音。所有板载硬件验证通过。

| 模块 | 验证结果 |
|------|----------|
| I2C + TCA9554PWR | OK |
| ST77916 圆屏 (QSPI 80MHz) | OK |
| CST816 触摸 (ChipID: 0xB5) | OK |
| LVGL v9.3 UI | OK |
| WiFi 2.4GHz | OK (需打开 iPhone 热点"最大兼容性") |
| I2S 双麦克风 (16kHz/16bit) | OK |
| PCM5101 喇叭 (I2S) | OK |

**踩坑记录**：
- PlatformIO 默认 `espressif32` 平台用的是 ESP-IDF 4.4，微雪驱动需要 ESP-IDF 5.x，需切换到 pioarduino 平台
- 从 GitHub 通过 WebFetch 下载的 `.h/.c` 文件中，尖括号 `<>` 的 include 会被 markdown 解析吞掉，需手动修复（如 `lv_conf.h` 中的 `<stdint.h>` 等）
- iPhone 热点默认用 5GHz，ESP32 只支持 2.4GHz，需打开"最大兼容性"

**关键驱动文件**（位于 `lib/waveshare/`，从微雪官方示例移植）：

| 文件 | 用途 |
|------|------|
| `Display_ST77916.h/.cpp` | ST77916 QSPI 屏幕驱动 |
| `esp_lcd_st77916.h/.c` | ESP-IDF LCD 底层驱动 |
| `TCA9554PWR.h/.cpp` | GPIO 扩展器驱动 |
| `Touch_CST816.h/.cpp` | 触摸屏驱动 |
| `I2C_Driver.h/.cpp` | I2C 通信封装 |
| `LVGL_Driver.h/.cpp` | LVGL 对接层 |

### Phase 2: 猫脸表情系统 - "让猫有表情" (已完成 2026-04-06)

**目标**：在 360x360 圆屏上用 LVGL 显示可切换的猫脸表情

- [x] 极简风格：纯黑背景 + 两只白色大圆眼睛，纯靠眼睛形状变化传达情绪
- [x] 实现 7 种表情：Neutral / Happy / Sad / Angry / Sleepy / Curious / Talking
- [x] 动画系统：自动眨眼（4~10s 随机间隔）、说话时眼睛律动、待机随机看向
- [x] 触摸交互：点击→开心 2s、长按→AI 对话、左右滑动→循环切换表情
- [ ] 利用 QMI8658 陀螺仪：摇晃板子触发晕眩表情（留到 Phase 6）

**完成总结**：

采用极简机器人/数码宠物风格——深色背景上两只白色大圆眼睛，所有情绪通过眼睛的尺寸、形状、位置、旋转来表达，没有任何额外 UI 元素。每只眼睛是一个 `lv_obj`，通过 `EyeState{cx, cy, w, h, rot}` 参数化控制。CST816 硬件手势检测驱动触摸交互（点击/滑动/长按）。

| 表情 | 眼睛效果 | 参数 (w×h, rot) |
|------|----------|-----------------|
| Neutral | 两个大白圆 ○ ○ | 95×95, 0° |
| Happy | 柔和眯眼 + 猫咪慢眨眼 | 95×55, 0°, Y-5；慢眨眼 350ms闭/400ms开 |
| Sad | 缩小下移 | 75×75, 0°, Y+12 |
| Angry | 扁窄+旋转 | 70×35, ±20° |
| Sleepy | 极细线 | 80×8, 0° |
| Curious | 一大一小不对称 | 左 70×70, 右 110×110 |
| Talking | 4 段循环律动（见下） | 圆眼挤压/拉伸节奏 |

**脸部布局**（360×360 圆屏）：

- 眼睛直径 95px，左眼中心 (113, 135)，右眼中心 (247, 135)
- 间距 39px，整体偏上以配合圆屏弧度
- 视觉层次：眼睛 100% 不透明，无其他 UI 元素

**动画系统**（基于 LVGL `lv_anim_t`，全部自驱动无需手动轮询）：

| 动画 | 时长 | 缓动曲线 | 说明 |
|------|------|----------|------|
| 表情切换 | 350ms | ease_in_out | 所有 5 个属性同时插值过渡 |
| 普通眨眼 | 100ms 闭 + 120ms 开 | ease_out | 4~10s 随机间隔自动循环 |
| Happy 慢眨眼 | 350ms 闭 + 400ms 开 | ease_in_out | 2.5~5s 间隔；高度压缩至 4px 薄线 |
| 待机看向 | 250ms 移 + 1s 停 + 250ms 回 | ease_in_out | 3~6s 随机间隔自动循环 |
| Talking 律动 | 4 段循环，总周期 ~450ms | 各段独立 | 无限循环，见下表 |

**Talking 眼睛律动（4 段 squeeze/unsquash 循环）**：

说话时眼睛进入无限重复的 4 段律动，通过挤压-拉伸-过冲-归位表现"在说话"的生命感，而非依赖外加嘴部控件：

| 段 | 描述 | 目标尺寸 | 旋转 | cy 偏移 | 时长 | easing |
|----|------|----------|------|---------|------|--------|
| 0 | 挤压上提 | 100×60 | ±10° | -10px | 100ms | ease_out |
| 1 | 展开下沉 | 95×95 | ±5° | +10px | 150ms | ease_out |
| 2 | 轻微过冲 | 98×98 | 0° | 0 | 100ms | ease_in_out |
| 3 | 归位 neutral | 95×95 | 0° | 0 | 100ms | linear |

律动在表情 morph（350ms）完成后启动，避免与入场动画打架。切换到其他表情时律动立即停止，通过 epoch 计数器保证无残留回调。

**Happy 猫咪慢眨眼（Slow Blink）原理**：猫行为学中，慢眨眼是信任和愉悦的信号。实现方式：基础状态为柔和眯眼（95×55，Y 上移 5px），在此基础上以 350ms 缓慢闭合（纯高度压缩至 4px 薄线），400ms 缓慢睁开恢复。间隔 2.5~5s 重复，用 ease_in_out 曲线保持柔和慵懒的节奏。Happy 状态下停止随机看向，保持满足的凝视。

**渲染管线**：

```
表情插值 (exprProgress 0→1000)
  → 叠加眨眼（纯高度压缩）
  → 叠加看向偏移（Talking 时固定 0）
  → _placeEye() 写入 lv_obj

Talking 额外层：
  talk exec_cb 直接调用 _placeEye()（绕过 render 管线的 lerp）
  右眼 done_cb 作为 master 推进 stage → (stage+1) % 4
```

切换表情时取消所有动画（expr/blink/look/talk），从当前插值状态平滑过渡到新目标，支持中途打断无跳变。开机时眼睛从 0px 渐大到正常尺寸（pop-in 效果）。

**性能配置**：

- LVGL 刷新率：`LV_DEF_REFR_PERIOD = 16ms`（~60fps）
- LVGL 任务轮询间隔：16ms（与刷新率对齐，避免 Core0 空转）
- LVGL heap：64KB 分配在 PSRAM，释放内部 SRAM 供系统使用
- `LV_DRAW_LAYER_MAX_MEMORY = 32KB`（防止复杂 layer 渲染 OOM）

**触摸交互**：

| 手势 | 效果 |
|------|------|
| 点击 | 切换到 Happy 2 秒后恢复 |
| 长按 | 触发 AI 对话（Phase 3+） |
| 右滑 / 下滑 | 下一个表情 |
| 左滑 / 上滑 | 上一个表情 |

**关键文件**：

| 文件 | 用途 |
|------|------|
| `src/cat_face.h` | CatExpression 枚举 + CatFace 类声明（含 EyeState + TalkStage） |
| `src/cat_face.cpp` | 极简猫脸绘制、7种表情、动画引擎、Talking 律动、手势交互 |

**踩坑记录**：
- **LVGL 触摸回调清除手势数据**：微雪的 `LVGL_Driver.cpp` 中 `my_touchpad_read` 每次读完触摸数据后会把 `touch_data.gesture` 清零为 `NONE`，导致主循环里永远读不到手势。修复：去掉 gesture 清零；同时让 `Touch_Read_Data()` 始终更新 gesture（包括 0x00=NONE），使手势结束后自然回到 NONE
- **CST816 手势方向不精确**：圆形触摸屏上滑动方向容易偏，只处理左右滑动会导致某些表情"卡住回不去"。修复：上下左右四个方向都绑定切换（右/下→下一个，左/上→上一个）
- **I2S 麦克风时钟串扰导致喇叭蜂鸣**：`mic_i2s_init()` 后 I2S 外设通过 DMA 持续在 SCK/WS 引脚产生高频时钟信号，在紧凑 PCB 上通过电磁耦合串扰到 PCM5101 DAC 音频输出通路，喇叭放大为可听见的蜂鸣声。修复：Phase 2 不需要音频时，不初始化任何 I2S 驱动，并将全部 6 个音频 I2S 引脚主动拉低
  - **Phase 4 应对方案**：I2S 必须"按需启停"，不可常驻运行。录音时才 `i2s_channel_enable()`，录完立即 `i2s_channel_disable()` + 拉低引脚；播放同理。录音和播放不要同时进行。如仍有残余噪声，可利用 PCM5101 的 XSMT 引脚做硬件静音
- **LVGL CAT_SLEEPY blink 死循环**：Sleepy 时 `_blinkAnimCb` 直接 return 跳过渲染，但 `_blinkDoneCb` 不检查 `_cur`，持续重新调度无效动画积累 heap 碎片。修复：`_scheduleBlink()` 加 `CAT_SLEEPY` 和 `CAT_TALKING` guard；done_cb 加 epoch 校验防止 `_apply()` 后的残留回调重新调度

### Phase 3: 云端通信 - "让猫联网" (已完成 2026-04-06)

**目标**：ESP32-S3 能通过 HTTPS 调用云端 LLM API

- [x] 封装 HTTP 客户端（HTTPS + JSON，通义千问 OpenAI 兼容接口）
- [x] 实现 WiFi 自动重连机制（断开后每 10s 重试 + 事件回调）
- [x] 测试调用 LLM API（长按触发对话，Talking 表情等待，回复后恢复 Neutral）
- [x] WiFi 凭证和 API Key 安全存储（`secrets.h` + `.gitignore`）
- [x] 全面稳定性和性能修复（见下）
- [ ] 可选：用 SD 卡缓存大音频文件（暂不需要，PSRAM 8MB 足够流式处理）

**完成总结**：

采用 FreeRTOS 后台 task 架构实现非阻塞 API 调用——主循环保持 LVGL 动画流畅运行，HTTP 请求在独立 task 中执行（绑定 Core 0 与 WiFi 协议栈同核）。通义千问 qwen-turbo 模型通过 OpenAI 兼容接口调用，响应体采用流式 JSON 解析（`deserializeJson(doc, http.getStream())`）避免中间 String 拷贝。WiFi 管理模块从 `main.cpp` 抽取为独立组件，支持事件驱动的状态跟踪和自动重连。

**交互流程**：

```
长按屏幕 → Talking 表情（眼睛律动） → FreeRTOS task 发送 HTTPS POST
→ 通义千问回复 → 回到 Neutral + Serial 打印回复（Phase 5 将由 AI 情感驱动表情）
→ 失败（超时/API 错误） → 回到 Neutral + Serial 打印错误
→ 无 WiFi → Curious 表情（猫咪疑惑）
```

**架构设计**：

| 组件 | 职责 |
|------|------|
| `wifi_manager` | WiFi 连接/重连/状态跟踪，事件回调驱动 |
| `cloud_client` | 通义千问 API 封装，FreeRTOS task 异步执行，主循环轮询结果 |
| `secrets.h` | WiFi 凭证 + API Key，gitignored 不入版本控制 |

**API 规格**：

- 通过 `secrets.h` 中的 `LLM_API_BASE`、`LLM_API_KEY`、`LLM_MODEL` 配置
- 支持任何 OpenAI 兼容接口（通义千问 / DeepSeek / OpenAI / MiniMax 等）
- System prompt：猫咪人设（简短回复 + 拟声词）
- 超时：15s，响应缓冲区 1KB (PSRAM)，Worker task 栈 16KB，绑定 Core 0

**稳定性与性能修复（本阶段系统性 review 结果）**：

| 类别 | 问题 | 修复 |
|------|------|------|
| Critical | CST816 `Touch_interrupts` 缺 `volatile`，编译器优化下任务侧永远读不到 ISR 写入 | 加 `volatile`；统一 ISR 宏为 `IRAM_ATTR` |
| Critical | Cloud worker task 栈 8KB，TLS 握手峰值 6~10KB，大概率溢出崩溃 | 栈扩大到 16KB；`xTaskCreate` → `xTaskCreatePinnedToCore(..., 0)` |
| Critical | `ESP_PANEL_HOST_SPI_MAX_TRANSFER_SIZE 2048` 远小于 partial buffer（25920 字节），flush 被拆分为数十笔事务 | 改为 `LCD_WIDTH × LCD_HEIGHT × 2`（259200） |
| Major | `CloudState _state` 用 `volatile`，Xtensa LX7 双核不保证 store-buffer 刷新顺序 | 改为 `std::atomic<CloudState>`，provides happens-before 语义 |
| Major | `http.getString()` 在 DRAM heap 制造完整响应副本，API 错误时可能 OOM | 改为流式解析 `deserializeJson(doc, http.getStream())`；加响应大小上限检查 |
| Major | LVGL task 每 5ms 唤醒，但刷新周期 16ms，约 60% 唤醒是空转占用 Core0 | task delay 改为 16ms |
| Major | LVGL 64KB heap 静态占用内部 SRAM | 通过 `LV_MEM_POOL_ALLOC = ps_malloc` 迁到 PSRAM |
| Major | `_blinkDoneCb` / `_lookDoneCb` 可在 `lv_anim_delete` 后仍被派发，导致双重调度 | 引入 `_animEpoch` 计数器，done-cb 校验 epoch，不匹配则丢弃 |

**关键文件**：

| 文件 | 用途 |
|------|------|
| `src/wifi_manager.h/.cpp` | WiFi 连接管理 + 自动重连 |
| `src/cloud_client.h/.cpp` | 通义千问 API 客户端（异步，Core 0 绑定） |
| `src/secrets.h` | WiFi/API 凭证（gitignored） |
| `src/secrets.h.example` | 凭证模板（入 Git） |

**已知待处理**（不影响当前功能，Phase 4 前修复）：
- `touch_data` 跨核访问（LVGL task 写 / loop task 读）无 mutex 保护
- HTTPS 使用 `setInsecure()`，生产环境需替换为根证书校验
- LCD flush callback 中 CPU 逐像素做字节序 swap，建议改为 DMA 异步完成回调

### Phase 4: 语音管道 - "让猫能听能说"

**目标**：实现完整的 语音输入 -> AI 回复 -> 语音输出 链路

- [ ] 双麦克风 I2S 录音 -> WAV/PCM 编码 -> 上传到后端 STT
- [ ] 后端返回 TTS 音频 -> I2S PCM5101 播放
- [ ] 实现 VAD（语音活动检测）：自动检测说话开始/结束
- [ ] 触摸屏作为"按住说话"备选方案
- [ ] 音量旋钮控制播放音量
- [ ] I2S 按需启停（避免麦克风时钟串扰喇叭）

### Phase 5: Multi-Agent 后端 - "给猫装上大脑"

**目标**：在 Mac 上搭建 Python 后端，管理多 Agent 和记忆

```mermaid
graph TD
    ESP["ESP32-S3 板子"] -->|"POST /chat (音频)"| API["Python FastAPI 后端"]
    API --> STT_svc["STT: Whisper/讯飞"]
    STT_svc -->|"转写文字"| Orchestrator["Agent 编排器"]
    Orchestrator --> ConvAgent["对话 Agent (猫猫人设)"]
    Orchestrator --> MoodAgent["情绪 Agent"]
    Orchestrator --> MemAgent["记忆 Agent"]
    ConvAgent -->|"查询记忆"| MemAgent
    MoodAgent -->|"分析情绪"| Orchestrator
    Orchestrator -->|"回复+情绪"| TTS_svc["TTS: Edge-TTS/讯飞"]
    TTS_svc -->|"音频+表情指令"| ESP
```

- [ ] **对话 Agent**：带猫猫人设的 LLM 对话（"我是一只傲娇的猫，喵~"）
- [ ] **情绪 Agent**：分析对话内容，返回情绪状态（happy/sad/angry/sleepy/curious）
- [ ] **记忆 Agent**：提取关键信息（用户名字、偏好、重要事件），长期存储
- [ ] SQLite 做记忆数据库
- [ ] 暴露 REST API：`POST /chat` 接收音频，返回 JSON（含音频 URL + 情绪）

### Phase 6: 集成与打磨 - "让猫活起来"

- [ ] ESP32 端主循环状态机（待机 -> 听 -> 思考 -> 说 -> 待机）
- [ ] 表情与语音同步（说话时眼睛律动，开心时眼睛弯成月牙）
- [ ] 待机时随机行为（眨眼、打哈欠、摇尾巴、看来看去）
- [ ] 开机动画（猫从睡梦中醒来）
- [ ] 优化延迟（录音结束到开始回复的时间）
- [ ] 错误处理：WiFi 断连恢复、API 超时重试
- [ ] 低电量提示表情
- [ ] QMI8658 陀螺仪：摇晃触发晕眩表情

## 当前代码结构

```
cyber_cat/
  platformio.ini
  README.md
  src/
    main.cpp                -- 主循环：init + 手势/云端状态轮询
    secrets.h               -- WiFi/API 凭证 (gitignored)
    secrets.h.example       -- 凭证模板
    cat_face.h/.cpp         -- LVGL 猫脸表情引擎（含 Talking 律动）
    cloud_client.h/.cpp     -- 云端 HTTP 通信（异步 FreeRTOS task）
    wifi_manager.h/.cpp     -- WiFi 连接管理
    lv_conf.h               -- LVGL 配置（heap→PSRAM, 60fps, etc.）
  lib/
    waveshare/
      Display_ST77916.h/.cpp  -- ST77916 QSPI 屏幕驱动
      esp_lcd_st77916.h/.c    -- ESP-IDF LCD 底层驱动
      TCA9554PWR.h/.cpp       -- GPIO 扩展器驱动
      Touch_CST816.h/.cpp     -- 触摸屏驱动（含 volatile ISR fix）
      I2C_Driver.h/.cpp       -- I2C 通信封装
      LVGL_Driver.h/.cpp      -- LVGL 对接层

  backend/                  -- Python 后端（Phase 5，Mac 上运行）
    main.py                 -- FastAPI 入口
    agents/                 -- 对话/情绪/记忆 Agent
    memory/                 -- SQLite 记忆存储
    requirements.txt        -- Python 依赖
```

## 开发顺序

严格按 **Phase 1 -> 2 -> 3 -> 4 -> 5 -> 6** 顺序推进，每个阶段都能独立验证。
