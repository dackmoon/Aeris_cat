[← 返回 README](../README.md) · [开发路线](roadmap.md)

# Phase 4: 语音管道 - "让猫能听能说" (已完成 2026-04-10)

**目标**：实现完整的 语音输入 -> STT -> AI 回复 -> TTS 语音输出 链路

- [x] 双麦克风 I2S 录音（16kHz 16bit mono PCM，PSRAM 缓冲最长 30s）
- [x] 实现 VAD（语音活动检测）：RMS 能量检测，2s 静音自动停止，300ms 最短语音防误触
- [x] PA 控制静音：录音期间 GPIO15=LOW 关闭功放，避免喇叭串扰（V2 I2S 持续运行供 ES7210 MCLK）
- [x] 对话状态机（IDLE → LISTENING → THINKING → TALKING → IDLE）集成到主循环
- [x] 触摸控制：长按开始录音，点击提前结束，再次长押取消
- [x] 新增 CAT_LISTENING / CAT_THINKING / CAT_TALKING 表情联动
- [x] **STT 音频上传**：PCM → 设备端构建 WAV 头 → multipart POST → SiliconFlow STT → transcript 自动喂给 LLM
- [x] **屏幕底部对话条幅**：自定义 GB2312 CJK 字体（3766 字），显示 `[你] 识别文字` 和 `[猫] AI 回复`，按圆屏弦宽定位（240px 宽，y≈300）
  - [x] 字幕自动消失：idle 后 8 秒定时清除 + 开始新录音时立即清除（`chatTickerClear()`）
  - [ ] 出现/消失动画（渐显渐隐或滑入滑出）
- [x] **WDT 崩溃修复**：HTTP worker 绑 Core 1 + 关闭 IDLE 任务 WDT 监控（见踩坑）
- [x] **TTS 语音合成播放**：AI 回复文字 → SiliconFlow CosyVoice2 → WAV → ES8311 DAC → 喇叭（已验证端到端播放）
  - ES8311 I2C 寄存器序列经多轮排查修正（见踩坑记录）
  - mono→stereo 转换、PA 控制（GPIO15）、DMA 清零、partial write 重试
  - 音量 API：`es8311SetVolume(0–100)`，TTS 播放期间临时提升到 70%，播放完恢复用户设定值
  - 调试工具：`TTS_DUMP_SERIAL=1` 可 hex dump WAV 到串口，配套 Python 脚本还原为 `.wav` 文件
  - 硬件自检：`es8311Beep(freq, duration)` 生成正弦波测试喇叭
- 剩余待办已移至 Phase 6（flush_skip 消除、音量旋钮、字幕动画等）

**已完成部分总结**：

V2 板使用 **ES7210 音频 ADC** + 双模拟麦克风，通过共享 I2S 总线采集音频。`cloud_client` 支持两种模式：**Phase 5 后端模式**（`BACKEND_URL` 宏定义时，POST 裸 WAV 到统一 `/chat` 接口，后端完成 STT→LLM→TTS 串联）和 **Phase 4 直连模式**（设备端自行调用 SiliconFlow STT + LLM chat + TTS 三个 API）。两种模式通过 `#ifdef BACKEND_URL` 条件编译切换，注释掉 `secrets.h` 中的 `BACKEND_URL` 即回退直连。整个流程在 Core 1 的独立 FreeRTOS task 中异步完成，主循环只需轮询 `cloud.poll()`。屏幕底部 CJK 条幅显示 `[猫]` AI 回复，idle 后 8 秒或开始新录音时自动清除。

**对话交互流程**：

Phase 5 模式（`BACKEND_URL` 已定义）：
```
长按  → LISTENING（VAD 监听，字幕清除，猫切 Listening 表情）
  自动/点击停止 → THINKING（猫切 Thinking）
  → PCM → WAV → POST 后端 /chat
  → 后端：STT → 记忆注入 → LLM → TTS → 情绪分析
  → JSON {text_reply, emotion, audio_url}
  → TALKING（猫切 emotion 表情，屏幕显示 [猫] 回复）
  → GET audio_url → ES8311 播放完毕 → IDLE（猫切 Neutral，8s 后字幕清除）
  再次长按（录音中）→ 取消，回到 IDLE
```

Phase 4 模式（`BACKEND_URL` 未定义，回退直连）：
```
长按  → LISTENING（VAD 监听，字幕清除，猫切 Listening 表情）
  自动/点击停止 → THINKING（猫切 Thinking，屏幕显示 [你] transcript）
  → PCM → WAV → SiliconFlow STT → transcript → LLM chat
  → AI 回复 → TALKING（猫切 Talking 眼睛律动，屏幕显示 [猫] 回复）
  → SiliconFlow TTS 下载 WAV → ES8311 播放完毕 → IDLE（猫切 Neutral，8s 后字幕清除）
  再次长按（录音中）→ 取消，回到 IDLE
```

**VAD 参数**（`audio_input.h`，可按实测麦克风调整）：

| 参数 | 值 | 说明 |
|------|----|------|
| `VAD_RMS_THRESHOLD` | 400 | 高于此 RMS = 有声音 |
| `VAD_SILENCE_MS` | 2000ms | 持续静音超过此时长 → 自动停止 |
| `VAD_MIN_SPEECH_MS` | 300ms | 最短有效语音，防止误触发 |
| `AUDIO_MAX_SECONDS` | 30s | 最长录音，超限强制停止 |

**API 配置**（`secrets.h`）：

| 宏 | 说明 |
|----|------|
| `BACKEND_URL` | **Phase 5 后端地址**（如 `http://122.51.129.89:8000`）。定义此宏即启用后端模式，注释掉即回退 Phase 4 直连 |
| `LLM_API_BASE` | LLM 接口基地址（Phase 4 直连用） |
| `LLM_API_KEY` | LLM API Key |
| `LLM_MODEL` | 模型名，如 `MiniMax-M2.5` |
| `STT_API_BASE` | STT 接口基地址（Phase 4 直连用） |
| `STT_API_KEY` | STT API Key（SiliconFlow） |
| `TTS_API_BASE` | TTS 接口基地址（Phase 4 直连 / Phase 5 fallback） |
| `TTS_API_KEY` | TTS API Key |
| `TTS_MODEL` | TTS 模型名 |
| `TTS_VOICE` | 音色名，如 `FunAudioLLM/CosyVoice2-0.5B:anna` |

Phase 5 模式下设备端只需 `BACKEND_URL` + TTS 配置（作为 `sendTTS` fallback）。TTS 最大缓冲 500KB PSRAM（约 15s @16kHz mono）。

**V2 板 ES7210 + ES8311 音频架构**：

```
Wire.begin() → es7210Init(16000)     → i2s_driver_install(RX+TX) → i2s_set_pin()
  ↓                ↓                         ↓                          ↓
 I2C 总线      ES7210/ES8311          遗留 I2S 驱动              MCLK/BCK/LRCK
              寄存器配置              (全双工 RX+TX)              DIN=GPIO39 (录音)
              (无需时钟)              (避免 GDMA 问题)            DOUT=GPIO47 (播放)

录音时：PA_CTRL=LOW → audioInputProcess() → PCM → STT
播放时：es8311Init() → PA_CTRL=HIGH → i2s_write(stereo) → PA_CTRL=LOW
```

| 配置项 | 值 | 说明 |
|--------|----|------|
| I2S 驱动 | 遗留 `driver/i2s.h` | 新标准驱动在 PSRAM(qio_opi) 下有 GDMA 兼容问题 |
| I2S 模式 | RX + TX 全双工 | 录音(DIN=GPIO39) 与播放(DOUT=GPIO47) 共用同一 I2S_NUM_0 实例 |
| 采样率 | 16kHz | MCLK = SR × 256 = 4.096MHz |
| 位深 | 16-bit stereo | 录音提取左声道(MIC1) mono；播放将 mono WAV 复制 L=R |
| ES7210 模拟增益 | 24dB (0x18) | 36dB 在近距离导致 ADC 饱和（0x7FFF 削波） |
| ES7210 数字音量 | 0dB (0xBF, 默认) | +32dB 与高增益叠加会严重削波 |
| ES8311 用户音量 | 20%（REG32=0x33） | `es8311SetVolume(20)` 设定基准；TTS 播放时临时 70%，完毕恢复 |
| TTS 缓冲 | 500KB PSRAM | 约 15s @16kHz 16-bit mono，heap_caps_malloc 从 SPIRAM 分配 |
| PA 控制 | GPIO15 HIGH/LOW | 播放前 HIGH（10ms 上电），播放完 LOW + 80ms drain 防 pop 音 |

**踩坑记录**：

- **ES7210 麦克风全零数据（V2 板最大坑）**：V2 板的麦克风不是 V1 的直连 MEMS 数字麦，而是通过 ES7210 ADC 芯片采集的双模拟麦克风。调通过程经历了多轮排查：
  1. **REG01 时钟关闭未恢复（根因）**：最初的 ES7210 初始化代码参考了 Espressif 开源驱动，在配置阶段写入 `REG01=0x3F` 关闭所有内部时钟，但**后续从未恢复**。对照 [Waveshare 官方 `08_esp_sr` 示例](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-1.85C/tree/main/Arduino/examples/08_esp_sr) 后发现官方代码**根本不写 REG01**。移除此行后数据立即正常
  2. **新 I2S 标准驱动 GDMA/PSRAM 冲突**：尝试使用 ESP-IDF 5.x 新的 `i2s_std.h` 驱动（含 Arduino `ESP_I2S.h` 封装），但在 PSRAM（qio_opi 模式）下 `i2s_channel_init_std_mode` 报错 `user context not in internal RAM`——GDMA 要求回调上下文在内部 SRAM，而 PSRAM 分配器将 I2S 内部结构放到了外部内存。该 SDK 配置在 Arduino 预编译框架下无法轻易修改，最终**回退到遗留 `driver/i2s.h` API**（不依赖 GDMA）解决
  3. **初始化顺序**：Waveshare 官方先配 ES7210 寄存器（I2C 不依赖时钟），再启动 I2S 提供 MCLK/BCK/LRCK。最终采用此顺序
  4. **增益与音量**：官方示例使用 36dB 模拟增益 + +32dB ADC 数字音量，但实测该组合在近距离说话时导致严重 ADC 饱和（采样值恒为 0x7FFF），VAD 永远检测不到静音，录音无法自动停止。最终降至 24dB 模拟增益 + 0dB 数字音量解决
  5. **录音卡住（ADC 削波 → VAD 失效）**：36dB+32dB 增益组合下，即使中等音量也会让 ADC 全程饱和，RMS 始终远超 `VAD_RMS_THRESHOLD`，`_silenceStartMs` 不断重置，录音永远不会自动结束。修复：降低增益 + 在 `audioInputProcess()` 中加入 `AUDIO_MAX_SECONDS` 硬时间限制强制停止
- **WDT 崩溃（Core 0 IDLE 饥饿）**：最初 HTTP worker task 绑定 Core 0，与 LVGL Loop 争核，导致 5 秒内 IDLE0 无法执行，看门狗触发重启。**第一步修复**：将 STT/Chat worker task 改为绑 Core 1（`xTaskCreatePinnedToCore(..., 1)`），栈从 16KB 扩至 32KB（TLS + 245KB 请求体需要更多栈）。**第二步修复**：TLS 上传期间 WiFi 驱动中断回调强制跑在 Core 0，导致 IDLE0 仍偶发饥饿；在 `setup()` 中调用 `esp_task_wdt_reconfigure()` 将 `idle_core_mask` 设为 0（停止监控 IDLE 任务），超时改为 30s。IDLE 饥饿在重度网络 I/O 期间是正常现象，无需 WDT 为此重启。
- **LVGL 渲染器死循环（对话后 UI 卡死的根因）**：对话完成后 UI 完全冻结——表情不回 Neutral、字幕不显示、触摸无响应。经过多轮诊断定位出**两个协同问题**：
  1. **`lv_timer_handler()` 无限重绘循环**：当 `_flush_skip=true` 导致 flush callback 瞬间完成（跳过 SPI 传输），LVGL display refresh timer 内部循环不断发现脏区域→渲染→瞬间 flush→发现新脏区域→永远不退出。这使 LVGL 任务卡死在单次 `lv_timer_handler()` 调用中。**修复**：skip mode 下完全跳过 `lv_timer_handler()`，LVGL callback 仍正常运行以缓存 UI 更新，恢复 normal mode 时一次性渲染
  2. **跨核竞态条件**：主循环（Core 1）在 `enterThinking()` 中设置 `_flush_skip=true` 时，LVGL 任务（Core 0）可能正在执行 `lv_timer_handler()`。标志在渲染中途变化会触发上述无限循环。**修复**：使用 `lvgl_port_lock()` 屏障——先获取 LVGL 互斥锁（等待当前渲染完成），在锁内设置 skip 标志，再释放锁。保证标志只在两次 LVGL 迭代之间改变

- **ES8311 DAC 无声音输出（仅功放 "咚" 一声）**：调用 `es8311Play()` 后喇叭仅在 PA 上电时震动一下，无实际语音播放。根因是 ES8311 I2C 寄存器初始化序列存在多处缺失/错误：
  1. **REG0x00 缺少 power-on 命令（0x80）**：soft reset 后只写了 0x1F→0x00，缺少最后的 0x80 power-on，芯片未真正启动
  2. **REG0x09/0x0A SDP 格式未配**：Serial Data Port 的输入/输出格式寄存器完全没写，导致 I2S 数据格式不匹配（16-bit 需写 0x0C）
  3. **REG0x0E=0xA0（错误）→ 0x02**：模拟通路配置错误，部分模拟模块未正确使能
  4. **REG0x13=0x00（根因之一）→ 0x10**：HP 驱动输出使能位为 0 = 静音，这是最直接的无声根因
  5. **REG0x37=0x48（错误）→ 0x08**：DAC 均衡器旁路和 fade-rate 位配置错误
  6. **REG0x01=0x30 → 0x3F**：内部时钟门控未全开，部分模块时钟被关断

  修复方法：对照 Espressif ESP-ADF `es8311.c` 参考驱动 + 数据手册逐寄存器比对。教训：音频编解码器的 I2C 寄存器初始化**必须对照官方参考驱动逐个验证**，一个位错误就可能导致完全静音
- **CJK 字体方框问题**：LVGL 内置 `lv_font_simsun_16_cjk` 仅含 1000 个常用部首，大量日常汉字（如"猫""你""喵"等）不在其中，渲染为方框 □。**修复**：用 [lv_font_conv](https://lvgl.io/tools/fontconverter) 从 STHeiti.ttf 生成自定义字体 `lv_font_chinese_16`，覆盖 GB2312 一级 3766 字 + CJK 标点 + 全角 ASCII，4bpp 16px，编译后增加约 320KB Flash。`chat_ticker.cpp` 中用 `stripEmoji()` 过滤 4 字节 UTF-8 序列（emoji 等），防止超出字体覆盖范围的字符显示为方框。

**架构技术债（Phase 6 待消除）**：

THINKING 期间 `flush_skip=true` 冻结整个 LVGL 渲染，Thinking 动画不播放。这个 hack 虽然修好了 LVGL 无限重绘 bug，但层层补丁。Phase 6 计划删除 flush_skip，并拆为 4 个 FreeRTOS Task（LVGL / State Machine / Audio / Cloud），用 Queue 替代 volatile flag，详见 Phase 6 待办。

> 常驻诊断框架（健康报告 / LVGL 心跳 / 超时 guard）见 [调试与测试](debugging-and-testing.md)。
