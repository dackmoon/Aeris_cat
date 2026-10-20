[← 返回 README](../README.md) · [开发路线](roadmap.md)

# Phase 3: 云端通信 - "让猫联网" (已完成 2026-04-06)

**目标**：ESP32-S3 能通过 HTTPS 调用云端 LLM API

- [x] 封装 HTTP 客户端（HTTPS + JSON，通义千问 OpenAI 兼容接口）
- [x] 实现 WiFi 自动重连机制（断开后每 10s 重试 + 事件回调）
- [x] 测试调用 LLM API（长按触发对话，猫保持当前表情静默等待，回复后恢复 Neutral）
- [x] WiFi 凭证和 API Key 安全存储（`secrets.h` + `.gitignore`）
- [x] 全面稳定性和性能修复（见下）
- [ ] 可选：用 SD 卡缓存大音频文件（暂不需要，PSRAM 8MB 足够流式处理）

**完成总结**：

采用 FreeRTOS 后台 task 架构实现非阻塞 API 调用——主循环保持 LVGL 动画流畅运行，HTTP 请求在独立 task 中执行（绑定 Core 1，避免与 Core 0 LVGL/WiFi 中断争用）。通义千问 qwen-turbo 模型通过 OpenAI 兼容接口调用，响应体采用流式 JSON 解析（`deserializeJson(doc, http.getStream())`）避免中间 String 拷贝。WiFi 管理模块从 `main.cpp` 抽取为独立组件，支持事件驱动的状态跟踪和自动重连。

**交互流程**：

```
长按屏幕（如 Sleepy 则先唤醒到 Neutral）
  → 猫保持当前表情 → FreeRTOS task 静默发送 HTTPS POST
  → 通义千问回复 → 回到 Neutral + Serial 打印回复（Phase 5 由 AI 情感驱动表情）
  → 失败（超时/API 错误） → 回到 Neutral + Serial 打印错误
  → 无 WiFi → 请求直接失败，猫不作额外反应
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
- 超时：15s，响应缓冲区 1KB (PSRAM)，Chat worker 栈 16KB，STT worker 栈 32KB，均绑定 Core 1

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
| `src/cloud_client.h/.cpp` | 通义千问 API 客户端（异步，Core 1 绑定） |
| `src/secrets.h` | WiFi/API 凭证（gitignored） |
| `src/secrets.h.example` | 凭证模板（入 Git） |

**已知待处理**（不影响当前功能，Phase 4 前修复）：
- `touch_data` 跨核访问（LVGL task 写 / loop task 读）无 mutex 保护
- HTTPS 使用 `setInsecure()`，生产环境需替换为根证书校验
- LCD flush callback 中 CPU 逐像素做字节序 swap，建议改为 DMA 异步完成回调
