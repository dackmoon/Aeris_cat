# Cyber Cat - AI 陪伴猫

基于微雪 ESP32-S3-Touch-LCD-1.85C 一体板 + 云端 AI，打造一只有表情、能听说、有记忆的 AI 陪伴猫。

ESP32-S3 作为薄客户端负责音频采集/播放和表情显示，所有 AI 推理通过云端完成。

## 硬件：微雪 ESP32-S3-Touch-LCD-1.85C **V2** 一体板

不需要任何接线！以下所有外设全部板载集成：

| 模块 | 芯片/型号 | 接口 & 引脚 |
|------|-----------|-------------|
| 主控 | ESP32-S3R8 | 双核 240MHz, 512KB SRAM, 8MB PSRAM, 16MB Flash |
| 屏幕 | ST77916, 1.85寸圆形 360x360, 262K色 | QSPI: SDA0=GPIO46, SDA1=GPIO45, SDA2=GPIO42, SDA3=GPIO41, SCK=GPIO40, CS=GPIO21, TE=GPIO18, RST=EXIO2, BL=GPIO5 |
| 触摸 | CST816 | I2C: SDA=GPIO11, SCL=GPIO10, INT=GPIO4, RST=EXIO1 |
| 音频 DAC (喇叭) | **ES8311** | 共享 I2S 总线, I2C 配置 |
| 音频 ADC (麦克风) | **ES7210** + 双模拟麦克风 | 共享 I2S 总线, I2C 地址 0x40 |
| 共享 I2S 总线 | — | MCLK=GPIO2, BCK=GPIO48, LRCK=GPIO38, DIN(mic)=GPIO39, DOUT(spk)=GPIO47 |
| 功放控制 | NS4150B | PA_CTRL=GPIO15 |
| GPIO 扩展器 | TCA9554PWR | I2C: 控制 LCD_RST, TP_RST, SD_CS 等 |
| 陀螺仪/加速度计 | QMI8658 | I2C: SCL=GPIO10, SDA=GPIO11, INT1=EXIO5, INT2=EXIO4 |
| RTC 时钟 | PCF85063 | I2C: SCL=GPIO10, SDA=GPIO11, INT=GPIO9 |
| SD 卡 | Micro SD 卡槽 | SD: MISO=GPIO16, MOSI=GPIO17, SCK=GPIO14, CS=EXIO3 |
| 电池管理 | 支持 3.7V 锂电池充放电 | MX1.25 2PIN 接口 |
| 其他 | 音量调节旋钮, USB Type-C, RESET/BOOT 按键 | - |

> **注意**: 本项目适配 **V2 版** 硬件。V1 版使用 PCM5101 DAC + MEMS 数字麦克风，引脚映射不同（V1 已于 2026.1.30 停产）。

**官方参考代码**：[waveshareteam/ESP32-S3-Touch-LCD-1.85C](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-1.85C)

## 开发环境

| 项 | 说明 |
|----|------|
| 构建工具 | [PlatformIO](https://platformio.org/) CLI（macOS: `brew install platformio`） |
| 平台 | pioarduino（Arduino ESP32 v3.1.3 + ESP-IDF 5.3），首次编译自动下载，见 `platformio.ini` |
| 依赖库 | LVGL ~9.3、ArduinoJson ^7（自动安装）；测试额外使用 Unity |
| 串口 | USB Type-C 直连，macOS 上为 `/dev/cu.usbmodem*`，115200 |
| WiFi | 仅支持 **2.4GHz**（iPhone 热点需打开"最大兼容性"） |
| 后端 | Python FastAPI，位于 `../cyber_cat_backend/`，当前部署在 `http://122.51.129.89:8000` |

首次配置凭证：

```bash
cp src/secrets.h.example src/secrets.h   # secrets.h 已 gitignore，不要提交
```

在 `src/secrets.h` 中填写 `WIFI_SSID_1` / `WIFI_PASS_1`、API Key 等。定义 `BACKEND_URL` 即走后端模式（STT/LLM/TTS 全由后端完成），注释掉则回退为设备直连 API 模式。各宏含义见 [phase4-voice.md](docs/phase4-voice.md) 的"API 配置"。

## 常用操作

```bash
pio run                      # 编译
pio run -t upload            # 编译并烧录
pio device monitor -b 115200 # 串口日志（Ctrl+C 退出）
pio test -e esp32s3          # 板上静音集成测试（会覆盖主固件，测完需重新 upload）
pio run -t clean             # 清理构建产物
```

- 烧录失败/端口找不到：按住 BOOT 键再按 RESET 进入下载模式后重试
- 正常运行时每 10s 输出一行 `[Health] main=N lvgl=N heap=N ds=X wifi=X`，含义见 [调试与测试](docs/debugging-and-testing.md)

## 文档地图

| 文档 | 内容 |
|------|------|
| [docs/architecture.md](docs/architecture.md) | 整体架构图、云端 API 选型、代码目录结构 |
| [docs/roadmap.md](docs/roadmap.md) | 开发路线总览 + Phase 6 待办 |
| [docs/phase1-hardware.md](docs/phase1-hardware.md) | Phase 1 硬件基础：驱动移植、硬件验证结果 |
| [docs/phase2-cat-face.md](docs/phase2-cat-face.md) | Phase 2 猫脸表情（v3 赛博扫描线引擎）：9 种情绪、物理弹簧、83fps 光栅化、触摸交互 |
| [docs/phase3-cloud.md](docs/phase3-cloud.md) | Phase 3 云端通信：HTTP 客户端、WiFi 重连、稳定性修复 |
| [docs/phase4-voice.md](docs/phase4-voice.md) | Phase 4 语音管道：录音/VAD、STT/TTS、ES7210/ES8311 音频架构、API 配置 |
| [docs/phase5-backend.md](docs/phase5-backend.md) | Phase 5 后端服务：`/chat` 接口合约、记忆策略、部署 |
| [docs/debugging-and-testing.md](docs/debugging-and-testing.md) | 串口日志、健康报告诊断框架、板上集成测试 |
| [docs/face-animation-redesign.md](docs/face-animation-redesign.md) | 表情与动画改造方案及 v2/v3 实测结果 |

各 Phase 文档末尾均有**踩坑记录**，排查问题时优先查阅。
