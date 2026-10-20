# 调试与测试

[← 返回 README](../README.md)

## 串口日志

```bash
pio device monitor -b 115200
```

板子使用 USB CDC 串口（`/dev/cu.usbmodem*`），烧录/复位后端口会重新枚举，开机前几行日志可能来不及看到。

## 常驻诊断框架

Phase 4 调试 LVGL 死循环后建立，永久保留。

| 机制 | 位置 | 说明 |
|------|------|------|
| 周期性健康报告 | `main.cpp` loop 末尾 | 每 10s 打印一行 `[Health]`，总览双核循环计数、剩余内存、对话状态、WiFi、帧率、表情 |
| LVGL 心跳计数器 | `LVGL_Driver.cpp` | `_lvglLoopCount` 每次 LVGL 循环 +1，通过 `lvgl_get_loop_count()` 暴露给主循环。健康报告中若计数停止增长 = Core 0 卡死 |
| `lv_timer_handler` 超时 guard | `LVGL_Driver.cpp` | 包裹 `lv_timer_handler()` 计时，超过 2s 打印 `[LVGL] WARN: lv_timer_handler stuck for Xms!`。第一时间暴露渲染卡顿 |

健康报告示例：
```
[Health] main=5810 lvgl=8992 heap=7520404 psram=7347540 ds=Idle wifi=1 fps=83.3 render_max=13ms face=Idle/Wink
```

| 字段 | 含义 |
|------|------|
| `main` / `lvgl` | 主循环（Core 1）/ LVGL 循环（Core 0）累计次数。持续增长 = 健康；某一侧停滞 = 对应核心卡死 |
| `heap` / `psram` | 剩余堆内存（含 PSRAM）/ 其中 PSRAM 部分 |
| `ds` | 对话状态：Idle / Listening / Thinking / Talking / Sleeping |
| `wifi` | 1 = 已连接 |
| `fps` | 过去 10s 实际推到屏幕的帧数 / 秒。v3 引擎下由于亚像素呼吸与连续微动，全状态稳定在 **70 ~ 83.3 fps** |
| `render_max` | 过去 10s 内单次 `lv_timer_handler()` 的最长耗时（常规 12~18ms，字幕刷新时约 29ms） |
| `face` | 当前表情的 Activity / Mood |

## 串口调试命令

通过串口发送一行命令（`pio device monitor` 中直接输入并回车）：

| 命令 | 作用 |
|------|------|
| `act <n>` | 强制表情的对话状态：0 Idle、1 Listening、2 Thinking、3 Talking、4 Sleeping（下次状态切换时被覆盖） |
| `mood <n>` | 设置情绪（0–8）：0 Neutral、1 Happy、2 Curious、3 Confused、4 Surprised、5 Love、6 Sad、7 Angry、8 Wink |
| `pulse <n>` | 短暂闪现某个情绪 1.5s（0–8） |
| `decor <mask>` | 装饰层掩码：1 高光块、2 赛博腮红 `///`、4 小嘴 `ω`，可相加（默认 3 = 高光+腮红，7 = 全部，0 = 关闭） |
| `tap` / `hold` | 模拟点击 / 长按 |
| `silent <0/1>` | 1 = 功放保持关闭（深夜测试用） |
| `chat` | 用内置语音样本走一遍完整对话（使用 `test-<MAC>` 会话，不写入猫的真实记忆） |
| `stress <n>` | 静音连续跑 n 轮假对话；每 4 轮里有一轮说话时被点击打断、一轮思考时被长按取消。结束打印 `[Stress] done: ok=N fail=N` |

静音压测：

```bash
pio device monitor -b 115200   # 然后输入 stress 12
```

2026-10-20 实测 `stress 12`：11 通过，1 次是后端首个请求超过 15s 读超时（走了 Confused → Idle 的错误路径），堆内存全程稳定。

## 表情模拟器

`../lvgl_sim/` 在 Mac 上直接编译 `src/cat_face.cpp`，调表情不用烧录。构建方法与按键见 `lvgl_sim/.claude/CLAUDE.md`。

- `./build/Release/lvgl_sim --shots <dir> [decor]`：渲染 12 种状态到 `<dir>/grid.ppm`，见 [v3 赛博表情总览](img/expressions-v3-cyber.png)（早期版本见 [v2 图](img/expressions-v2.png)、[v1 基线图](img/baseline-expressions.png)）
- `./build/Release/lvgl_sim --decor-shots <dir>`：渲染装饰层四档对比到 `<dir>/decor.ppm`，见 [v3 装饰对比](img/decor-v3-cyber.png)

截图模式会关闭随机眨眼和扫视，保证每次输出一致。

## 板上集成测试（静音）

`test/test_pipeline/` 是一套跑在板子上的 Unity 测试，不使用麦克风和喇叭：用预先合成的中文语音样本（`src/speech_sample.h`，固件的 `chat` / `stress` 命令也用它；macOS `say -v Tingting` 生成，16kHz/16bit/mono）代替真人说话，走与固件相同的后端链路。

```bash
pio test -e esp32s3      # 编译测试固件 → 烧录 → 读取结果（会覆盖主固件）
pio test -e esp32s3 -v   # 同上，额外打印 [test] 详细日志（I2C 扫描、后端回复等）
pio run -t upload        # 测试完成后烧回主固件
```

| 测试项 | 检查内容 |
|--------|----------|
| `test_psram_available` | PSRAM ≥ 4MB |
| `test_i2c_devices_respond` | TCA9554 (0x20)、CST816 (0x15)、ES8311 (0x18)、ES7210 (0x40)、PCF85063 (0x51) 应答 |
| `test_wifi_connects` | 用 `secrets.h` 中的 `WIFI_SSID_1` / `WIFI_PASS_1` 连接 |
| `test_backend_health` | `GET {BACKEND_URL}/health` 返回 200 |
| `test_backend_chat_pipeline` | `POST /chat` 语音样本 → STT → LLM → TTS，校验 `text_reply` / `emotion` / `audio_url` |
| `test_tts_audio_download` | 下载 `audio_url`，校验为 16kHz WAV（只下载不播放）|

说明：
- 测试使用独立会话 ID `test-<MAC>`，不污染猫的正常对话记忆
- 未定义 `BACKEND_URL`（直连模式）时，后端相关测试自动跳过
- 未覆盖：真实麦克风录音、喇叭播放、屏幕显示、触摸手势
- QMI8658 陀螺仪在本板 0x6A/0x6B 均无应答，固件暂未使用，因此不纳入检查

替换语音样本：

```bash
say -v Tingting -o speech.wav --file-format=WAVE --data-format=LEI16@16000 "要说的话"
# 再将 PCM 数据转成 src/speech_sample.h 中的 SPEECH_PCM 数组
```
