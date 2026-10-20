[← 返回 README](../README.md) · [开发路线](roadmap.md)

# Phase 1: 硬件基础 - "让猫睁开眼睛" (已完成 2025-04-04)

**目标**：验证所有硬件能正常工作，跑通微雪官方驱动

- [x] 修改 `platformio.ini`：配置 ESP32-S3R8 参数（PSRAM、Flash、分区表等）
- [x] 连接 WiFi 并在串口打印 IP 地址
- [x] 移植微雪官方 ST77916 + TCA9554PWR 驱动，点亮圆屏
- [x] 集成 LVGL 图形库，在圆屏上显示内容
- [x] 测试 I2S 双麦克风录音（串口打印音量值）
- [x] 测试 I2S 音频播放（V2: ES8311 DAC + NS4150B 功放）
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
| I2S 麦克风 (16kHz/16bit, V2: ES7210 + 双模拟麦) | OK |
| I2S 喇叭 (V2: ES8311 DAC + NS4150B 功放) | OK |

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
