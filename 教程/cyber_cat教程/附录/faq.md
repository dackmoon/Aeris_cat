# 附录 A：常见问题 (FAQ)

> 最后更新：2026-04-11  
> 适用版本：Cyber Cat Phase 4/5

---

## 编译问题

### Q1: `lvgl.h: No such file or directory`

**原因：** LVGL 库未安装或路径配置错误。

**解决方案：**
```bash
# 1. 通过 PlatformIO 安装
pio lib install "lvgl/lvgl@~9.3.0"

# 2. 检查 platformio.ini
[env:esp32s3]
lib_deps =
    lvgl/lvgl@~9.3.0

# 3. 清理重新编译
pio run --target clean
pio run
```

### Q2: `ps_malloc` 未定义

**原因：** 未启用 PSRAM 支持。

**解决方案：**
```ini
; platformio.ini
[env:esp32s3]
build_flags =
    -D BOARD_HAS_PSRAM
    -D ARDUINO_USB_CDC_ON_BOOT=1
```

### Q3: 编译时内存不足

**原因：** ESP32-S3 分区表配置问题或代码过大。

**解决方案：**
```ini
; 使用更大的 APP 分区
board_build.partitions = large_app.csv

; 或启用编译优化
build_flags =
    -Os  ; 优化大小
```

### Q4: Python 依赖安装失败

**原因：** pip 版本过旧或网络问题。

**解决方案：**
```bash
# 升级 pip
pip3 install --upgrade pip

# 使用国内镜像
pip3 install -r requirements.txt -i https://pypi.tuna.tsinghua.edu.cn/simple

# 或使用虚拟环境
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt
```

---

## 烧录问题

### Q1: `No serial port found`

**原因：** USB 驱动未安装或线缆问题。

**解决方案：**
1. 确保使用 **USB-C 数据线**（有些线只充电）
2. 安装 CH342/CH343 驱动（根据开发板芯片）
3. 检查设备管理器：
   ```bash
   # Linux
   ls /dev/ttyUSB*  # 或 ls /dev/ttyACM*
   
   # 查看权限
   ls -l /dev/ttyUSB0
   
   # 添加用户到 dialout 组
   sudo usermod -a -G dialout $USER
   # 然后重新登录
   ```

### Q2: 烧录失败 `Failed to connect to ESP32-S3`

**原因：** bootloader 模式未进入。

**解决方案：**
```bash
# 方法 1：按住 BOOT 键，点击 RESET，然后烧录
# 方法 2：使用烧录按钮（如有）

# 手动进入 bootloader
esptool.py --port /dev/ttyUSB0 chip_id

# 如果上面成功，再烧录
pio run --target upload
```

### Q3: 烧录后无法启动

**原因：** 电源不足或启动引脚电平问题。

**解决方案：**
1. 检查电源：确保 USB 供电≥500mA
2. 检查 GPIO0：启动时应为高电平
3. 查看启动日志：
   ```bash
   pio device monitor -b 115200
   ```

---

## 硬件问题

### Q1: 屏幕不显示

**排查步骤：**
1. 检查 SPI 接线（MOSI, CLK, CS, DC, RST）
2. 测量背光电压（3.3V 或 5V）
3. 确认 LVGL 初始化成功：
   ```cpp
   if (!lv_init()) {
       Serial.println("LVGL 初始化失败");
   }
   ```

### Q2: 触摸无响应

**排查步骤：**
1. 检查 I2C 接线（SDA, SCL）
2. 扫描 I2C 设备：
   ```cpp
   void scanI2C() {
       for (byte addr = 1; addr < 127; addr++) {
           Wire.beginTransmission(addr);
           if (Wire.endTransmission() == 0) {
               Serial.printf("找到设备：0x%02X\n", addr);
           }
       }
   }
   ```
3. 调整触摸阈值：
   ```cpp
   #define TOUCH_THRESH 1000  // 尝试增大或减小
   ```

### Q3: 麦克风无声

**排查步骤：**
1. 检查 I2S 接线（BCLK, LRCLK, DIN, MCLK）
2. 确认 ES7210 供电（3.3V）
3. 检查 I2S 配置：
   ```cpp
   i2s_config_t i2s_config = {
       .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
       .sample_rate = 16000,
       .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
       // ...
   };
   ```

### Q4: WiFi 信号弱

**排查步骤：**
1. 检查天线连接（如有外接天线座）
2. 远离金属物体
3. 使用 USB 集线器供电（避免 USB 干扰）

---

## API Key 问题

### Q1: SiliconFlow 报错 `401 Unauthorized`

**原因：** API Key 无效或过期。

**解决方案：**
1. 登录 https://cloud.siliconflow.cn 检查账户
2. 确认 Key 有足够额度
3. 检查 `.env` 文件：
   ```bash
   SILICONFLOW_API_KEY=sk-xxxxxxxxxxxx
   ```

### Q2: DashScope 报错 `InvalidApiKey`

**原因：** 阿里云百炼 API Key 配置错误。

**解决方案：**
1. 访问 https://dashscope.console.aliyun.com
2. 创建/复制 API Key
3. 添加到环境变量：
   ```bash
   export DASHSCOPE_API_KEY="sk-xxxxx"
   ```

### Q3: API 额度不足

**临时方案：**
- 降低调用频率（增加间隔）
- 使用更便宜的模型
- 缓存重复请求结果

**长期方案：**
- 充值账户
- 申请教育/开源资助

---

## WiFi 连接问题

### Q1: 无法连接热点

**排查：**
```cpp
// 添加调试日志
WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
Serial.print("正在连接");

int timeout = 30000;
while (WiFi.status() != WL_CONNECTED && timeout > 0) {
    delay(100);
    Serial.print(".");
    timeout -= 100;
}

if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n已连接");
} else {
    Serial.println("\n连接失败");
    Serial.printf("错误码：%d\n", WiFi.status());
}
```

**常见错误码：**
- `WL_CONNECT_FAILED` (4)：密码错误或信号太弱
- `WL_CONNECTION_LOST` (5)：连接后断开

### Q2: 频繁断开连接

**解决方案：**
1. 检查电源稳定性
2. 增加 WiFi 重连逻辑：
   ```cpp
   void checkWiFi() {
       if (WiFi.status() != WL_CONNECTED) {
           Serial.println("WiFi 断开，重连中...");
           WiFi.reconnect();
       }
   }
   ```
3. 降低发射功率（减少干扰）：
   ```cpp
   WiFi.setTxPower(WIFI_POWER_13dBm);  // 默认 20dBm
   ```

---

## 音频问题

### Q1: 录音有杂音

**原因：** 电源噪声或增益过高。

**解决方案：**
```cpp
// 降低 ES7210 增益
writeES7210(0x01, 0x2C);  // 尝试 0x1C, 0x0C
```

### Q2: 播放无声

**排查：**
1. 检查 ES8311 I2C 配置
2. 确认 I2S MCLK 连接
3. 检查扬声器阻抗（应为 4-8Ω）

---

## 内存问题

### Q1: `malloc failed` / `OOM`

**原因：** Heap/PSRAM 不足。

**解决方案：**
```cpp
// 1. 检查内存使用
Serial.printf("Free Heap: %d\n", ESP.getFreeHeap());
Serial.printf("Free PSRAM: %d\n", ESP.getFreePsram());

// 2. 使用 PSRAM 分配大对象
uint8_t* buffer = (uint8_t*)ps_malloc(size);

// 3. 释放不再使用的对象
lv_obj_delete(old_obj);
free(buffer);
```

### Q2: 重启 `Guru Meditation Error`

**原因：** 栈溢出或访问非法内存。

**解决方案：**
1. 查看崩溃回溯：
   ```
   Backtrace: 0x400d1234:0x3ffb0000
   ```
2. 使用 `esp_exception_decoder` 解析
3. 增加栈大小：
   ```cpp
   xTaskCreate(taskFunc, "TaskName", 8192, NULL, 1, NULL);
   ```

---

## 后端问题

### Q1: `Address already in use`

**原因：** 端口被占用。

**解决方案：**
```bash
# 查看占用进程
lsof -i :8000

# 杀死进程
kill -9 <PID>

# 或更换端口
uvicorn main:app --port 8001
```

### Q2: `sqlite3.OperationalError: database is locked`

**原因：** 数据库并发访问。

**解决方案：**
```python
# 1. 启用 WAL 模式
cursor.execute("PRAGMA journal_mode=WAL")

# 2. 使用连接池
# 3. 减少事务持有时间
```

### Q3: 服务无法启动 `systemctl`

**排查：**
```bash
# 查看状态
systemctl status cybercat-backend

# 查看日志
journalctl -u cybercat-backend -f

# 手动测试
cd /opt/cybercat_backend
source venv/bin/activate
python3 main.py
```

---

## LVGL 问题

### Q1: 屏幕闪烁

**原因：** 刷新率过低或缓冲区太小。

**解决方案：**
```cpp
// lv_conf.h
#define LV_DISP_DEF_REFR_PERIOD 16  // 60fps

#define LV_MEM_SIZE (64 * 1024)  // 增大内存
```

### Q2: 动画卡顿

**原因：** CPU 占用过高。

**解决方案：**
1. 减少同时运行的动画数量
2. 降低 LVGL 颜色深度：
   ```ini
   build_flags =
     -D LV_COLOR_DEPTH=16
   ```

---

## 其他问题

### Q: 项目文件结构混乱

**建议：**
```
cyber_cat/
├── src/              # 固件源码
├── lib/              # 固件库
├── tests/            # 固件测试
├── platformio.ini    # PlatformIO 配置
└── docs/             # 文档

cyber_cat_backend/
├── main.py           # 入口文件
├── services/         # 业务服务
├── memory/           # 记忆系统
├── utils/            # 工具函数
├── tests/            # 测试
└── requirements.txt  # Python 依赖
```

### Q: 如何调试生产环境问题？

**建议：**
1. 启用远程日志（发送到服务器）
2. 添加健康检查端点
3. 使用 OTA 诊断模式
4. 收集崩溃报告

---

## 获取帮助

如果以上 FAQ 未能解决问题：

1. **查看官方文档**
   - [ESP32-S3 技术文档](https://www.espressif.com.cn/)
   - [LVGL 文档](https://docs.lvgl.io/)
   - [FastAPI 文档](https://fastapi.tiangolo.com/)

2. **搜索类似问题**
   - GitHub Issues
   - Stack Overflow
   - 相关论坛

3. **提交 Issue**
   - 提供详细的错误日志
   - 说明复现步骤
   - 附上环境信息（ESP-IDF 版本、Python 版本等）

---

祝你问题解决顺利！🔧
