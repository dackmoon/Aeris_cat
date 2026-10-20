# 开发路线

[← 返回 README](../README.md)

严格按 **Phase 1 -> 2 -> 3 -> 4 -> 5 -> 6** 顺序推进，每个阶段都能独立验证。Phase 1–5 已全部完成，Phase 6 为持续打磨。

| 阶段 | 主题 | 状态 | 文档 |
|------|------|------|------|
| Phase 1 | 硬件基础 - "让猫睁开眼睛" | 已完成 2025-04-04 | [phase1-hardware.md](phase1-hardware.md) |
| Phase 2 | 猫脸表情系统 - "让猫有表情" | 已完成 2026-04-07 | [phase2-cat-face.md](phase2-cat-face.md) |
| Phase 3 | 云端通信 - "让猫联网" | 已完成 2026-04-06 | [phase3-cloud.md](phase3-cloud.md) |
| Phase 4 | 语音管道 - "让猫能听能说" | 已完成 2026-04-10 | [phase4-voice.md](phase4-voice.md) |
| Phase 5 | 后端服务 - "给猫装上大脑" | 已完成 2026-04-11 | [phase5-backend.md](phase5-backend.md) |
| Phase 6 | 集成与打磨 - "让猫活起来" | 进行中 | 见下 |

## Phase 6: 集成与打磨 - "让猫活起来"

> 表情、动画、渲染相关的待办已整合进 [表情与动画改造方案](face-animation-redesign.md)。

- [x] ESP32 端主循环状态机（IDLE → LISTENING → THINKING → TALKING → IDLE）
- [x] 表情与语音同步（说话时双包络提取音节拍点驱动眼睛弹跳，后端 emotion 驱动情绪）
- [x] 待机时随机行为（眨眼、扫视、5 种自主微表情小剧场，75s 渐困、120s 入睡）
- [x] 删除 `flush_skip` / `touch_skip` 机制，片上 DMA SRAM 双缓冲 + LUT 扫描线光栅化，全状态稳定 70~83fps
- [ ] 设备端重构：拆为 4 个 FreeRTOS Task（LVGL / State Machine / Audio / Cloud），volatile flag → Queue
- [ ] 后端 `/chat` 增加 `transcript` 字段，修复 Phase 5 模式下 `[你]` 用户字幕不显示
- [ ] 情绪分析升级为 LLM 分类（替代规则匹配）
- [ ] 字幕出现/消失动画（渐显渐隐或滑入滑出）
- [ ] 音量旋钮控制播放音量
- [ ] 开机动画（猫从睡梦中醒来）
- [ ] 优化延迟（录音结束到开始回复的时间）
- [ ] 错误处理：WiFi 断连恢复、API 超时重试
- [ ] 低电量提示表情
- [ ] QMI8658 陀螺仪：摇晃触发晕眩表情
- [ ] HTTPS 通信（nginx + TLS 证书，替代当前 HTTP 明文）
