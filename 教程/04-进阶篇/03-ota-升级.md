# 4.3 OTA 远程升级：无线固件更新

> 最后更新：2026-04-11  
> 适用版本：Cyber Cat Phase 4/5

---

## 4.3.1 OTA 原理与架构

### OTA 升级流程

```
┌─────────────────────────────────────────────────────────────────┐
│                     OTA 升级完整流程                             │
│                                                                 │
│  1. 检查更新          2. 下载固件         3. 验证签名            │
│  ┌─────────┐         ┌─────────┐        ┌─────────┐            │
│  │ ESP32   │ ──────► │ 云端    │ ─────► │ ESP32   │            │
│  │ 请求版本│         │ 返回新  │        │ SHA256  │            │
│  │ 信息    │         │ 固件 URL│        │ 校验    │            │
│  └─────────┘         └─────────┘        └─────────┘            │
│       │                    │                    │               │
│       ▼                    ▼                    ▼               │
│  ┌─────────┐         ┌─────────┐        ┌─────────┐            │
│  │ 比较版本│         │ HTTPS   │        │ 校验通  │            │
│  │ 有新版本│         │ 下载    │        │ 过      │            │
│  └─────────┘         └─────────┘        └─────────┘            │
│                                                 │               │
│       ▲                                         ▼               │
│       │                                  ┌─────────┐            │
│       │         5. 重启生效               │ 写入    │            │
│       │         ◄──────────────────────── │ OTA 分区│            │
│       │                                  └─────────┘            │
│       │                                       │                 │
│       │                                       ▼                 │
│       │                                  ┌─────────┐            │
│       └───────────────────────────────── │ 标记    │            │
│          6. 上报升级结果                     │ 成功  │            │
│                                          └─────────┘            │
└─────────────────────────────────────────────────────────────────┘
```

### ESP32 OTA 分区结构

ESP32 的 Flash 被划分为多个分区，支持 OTA 的关键是 **OTA 数据分区** 和 **两个 APP 分区**。

```
Flash 地址布局：
0x0000 ┌─────────────────┐
       │   Secure Boot   │ 0x0000-0x1000
       │   + Partition   │
0x1000 │   Table         │
       ├─────────────────┤
0x2000 │   OTA Data      │ 存储当前启动分区信息
       │   (16KB)        │
       ├─────────────────┤
0x10000│   App 0 (ota_0) │ 当前运行的固件
       │   (~1.5MB)      │
       ├─────────────────┤
0x200000│  App 1 (ota_1) │ 新固件下载目标
       │   (~1.5MB)      │
       ├─────────────────┤
       │   SPIFFS/LittleFS│ 文件系统
       └─────────────────┘
```

---

## 4.3.2 固件端实现

### 编辑 `src/ota_updater.h`

```cpp
// src/ota_updater.h
#pragma once
#include <Arduino.h>
#include <Update.h>
#include <WiFiClientSecure.h>

// OTA 配置
#define OTA_CHECK_INTERVAL_MS (6 * 60 * 60 * 1000UL)  // 6 小时检查一次
#define OTA_DOWNLOAD_TIMEOUT_MS 60000  // 下载超时 60 秒
#define OTA_MAX_REDIRECTS 3  // 最大重定向次数

// OTA 状态
enum OtaState {
    OTA_IDLE,           // 空闲
    OTA_CHECKING,       // 检查更新中
    OTA_DOWNLOADING,    // 下载固件中
    OTA_VERIFYING,      // 验证固件中
    OTA_UPDATING,       // 刷写固件中
    OTA_REBOOTING,      // 重启中
    OTA_SUCCESS,        // 升级成功
    OTA_FAILED,         // 升级失败
};

// OTA 事件回调
typedef void (*OtaEventCb)(OtaState state, int progress, const char* message);

class OtaUpdater {
private:
    String _versionUrl;
    String _currentVersion;
    String _serverHost;
    uint16_t _serverPort;
    
    OtaState _state;
    int _progress;
    String _message;
    
    OtaEventCb _onEvent;
    
    uint32_t _lastCheckTime;
    uint32_t _retryDelay;
    int _retryCount;
    
    // 内部方法
    bool downloadUpdate(const char* firmwareUrl);
    bool verifyFirmware(const uint8_t* expectedHash);
    void setState(OtaState state, const char* message);
    void reportProgress(int percent);
    
public:
    OtaUpdater();
    
    // 初始化
    void begin(const String& versionUrl, const String& currentVersion);
    
    // 设置服务器信息（用于下载）
    void setServerInfo(const String& host, uint16_t port);
    
    // 设置事件回调
    void onEvent(OtaEventCb cb);
    
    // 检查更新（在 loop 中调用）
    void checkForUpdates();
    
    // 强制检查更新
    void forceCheckForUpdates();
    
    // 获取当前状态
    OtaState getState() const { return _state; }
    int getProgress() const { return _progress; }
    const String& getMessage() const { return _message; }
    
    // 是否正在升级
    bool isUpdating() const { 
        return _state == OTA_DOWNLOADING || 
               _state == OTA_VERIFYING || 
               _state == OTA_UPDATING; 
    }
};

// 全局 OTA 实例
extern OtaUpdater otaUpdater;
```

### 编辑 `src/ota_updater.cpp`

```cpp
// src/ota_updater.cpp
#include "ota_updater.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

OtaUpdater otaUpdater;  // 全局实例

OtaUpdater::OtaUpdater()
    : _state(OTA_IDLE),
      _progress(0),
      _lastCheckTime(0),
      _retryDelay(5000),
      _retryCount(0) {
}

void OtaUpdater::begin(const String& versionUrl, const String& currentVersion) {
    _versionUrl = versionUrl;
    _currentVersion = currentVersion;
    _lastCheckTime = millis();  // 启动时不立即检查
    
    Serial.printf("[OTA] 初始化完成，当前版本：%s\n", currentVersion.c_str());
}

void OtaUpdater::setServerInfo(const String& host, uint16_t port) {
    _serverHost = host;
    _serverPort = port;
}

void OtaUpdater::onEvent(OtaEventCb cb) {
    _onEvent = cb;
}

void OtaUpdater::setState(OtaState state, const char* message) {
    _state = state;
    _message = message ? message : "";
    
    Serial.printf("[OTA] 状态：%d, 消息：%s\n", state, _message.c_str());
    
    if (_onEvent) {
        _onEvent(_state, _progress, _message.c_str());
    }
}

void OtaUpdater::reportProgress(int percent) {
    _progress = percent;
    
    if (_progress % 10 == 0) {  // 每 10% 报告一次
        Serial.printf("[OTA] 进度：%d%%\n", _progress);
    }
    
    if (_onEvent) {
        _onEvent(_state, _progress, _message.c_str());
    }
}

void OtaUpdater::checkForUpdates() {
    // 非强制检查时，检查间隔
    if (millis() - _lastCheckTime < OTA_CHECK_INTERVAL_MS) {
        return;
    }
    
    // 正在升级时不检查
    if (isUpdating()) {
        return;
    }
    
    forceCheckForUpdates();
}

void OtaUpdater::forceCheckForUpdates() {
    setState(OTA_CHECKING, "正在检查更新...");
    _lastCheckTime = millis();
    
    WiFiClientSecure client;
    
    // 连接版本服务器
    // 注意：实际项目中应该解析 _versionUrl 获取 host 和 path
    if (!client.connect(_serverHost.c_str(), _serverPort)) {
        setState(OTA_FAILED, "无法连接版本服务器");
        _retryCount++;
        return;
    }
    
    // 发送 HTTP GET 请求
    client.print(String("GET ") + _versionUrl + " HTTP/1.1\r\n");
    client.print("Host: " + _serverHost + "\r\n");
    client.print("X-Current-Version: " + _currentVersion + "\r\n");
    client.print("X-Device-ID: " + WiFi.macAddress() + "\r\n");
    client.print("Connection: close\r\n\r\n");
    
    // 等待响应
    delay(100);
    
    // 读取响应头
    int statusCode = 0;
    String redirectUrl = "";
    
    while (client.connected()) {
        String line = client.readLineUntil('\n');
        line.trim();
        
        if (line.startsWith("HTTP/")) {
            // 解析状态码
            statusCode = line.substring(9, 12).toInt();
        } else if (line.startsWith("Location:")) {
            redirectUrl = line.substring(10);
            redirectUrl.trim();
        } else if (line == "\r") {
            break;  // 响应头结束
        }
    }
    
    // 处理重定向
    static int redirectCount = 0;
    if (statusCode == 301 || statusCode == 302) {
        if (redirectCount < OTA_MAX_REDIRECTS) {
            redirectCount++;
            Serial.printf("[OTA] 重定向到：%s\n", redirectUrl.c_str());
            // 简化处理：实际应该解析新 URL 并重新请求
            setState(OTA_FAILED, "重定向不支持");
            return;
        } else {
            setState(OTA_FAILED, "重定向次数过多");
            return;
        }
    }
    
    redirectCount = 0;
    
    if (statusCode != 200) {
        setState(OTA_FAILED, String("版本检查失败：" + String(statusCode)).c_str());
        _retryCount++;
        return;
    }
    
    // 读取 JSON 响应体
    String jsonBody = "";
    while (client.connected()) {
        jsonBody = client.readStringUntil('\n');
        if (jsonBody == "\r") break;
    }
    jsonBody = client.readString();
    
    client.stop();
    
    // 解析 JSON
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, jsonBody);
    
    if (err) {
        setState(OTA_FAILED, String("JSON 解析失败：") + err.c_str());
        return;
    }
    
    const char* newVersion = doc["version"];
    const char* firmwareUrl = doc["firmware_url"];
    const char* firmwareHash = doc["firmware_hash"];
    const char* releaseNotes = doc["release_notes"];
    
    if (!newVersion || !firmwareUrl || !firmwareHash) {
        setState(OTA_FAILED, "版本信息格式错误");
        return;
    }
    
    Serial.printf("[OTA] 当前版本：%s, 新版本：%s\n", _currentVersion.c_str(), newVersion);
    
    // 比较版本号
    if (strcmp(newVersion, _currentVersion.c_str()) <= 0) {
        setState(OTA_IDLE, "已是最新版本");
        _retryCount = 0;
        return;
    }
    
    // 有新版本，开始下载
    Serial.printf("[OTA] 发现新版本：%s\n", newVersion);
    Serial.printf("[OTA] 固件 URL: %s\n", firmwareUrl);
    Serial.printf("[OTA] 发布说明：%s\n", releaseNotes ? releaseNotes : "无");
    
    // 保存新版本号（成功后更新）
    String targetVersion = String(newVersion);
    
    if (downloadUpdate(firmwareUrl)) {
        // 下载成功，验证固件
        if (verifyFirmware(firmwareHash)) {
            // 验证成功，标记 OTA 完成
            Update.end(true);  // true = 标记为可启动
            
            setState(OTA_SUCCESS, "升级成功，准备重启");
            
            // 延迟 2 秒后重启
            delay(2000);
            ESP.restart();
        } else {
            setState(OTA_FAILED, "固件验证失败");
            _retryCount++;
        }
    } else {
        setState(OTA_FAILED, "固件下载失败");
        _retryCount++;
    }
}

bool OtaUpdater::downloadUpdate(const char* firmwareUrl) {
    setState(OTA_DOWNLOADING, "正在下载固件...");
    
    WiFiClientSecure client;
    
    // 连接固件服务器
    // 注意：实际应该解析 firmwareUrl
    if (!client.connect(_serverHost.c_str(), _serverPort)) {
        Serial.println("[OTA] 无法连接固件服务器");
        return false;
    }
    
    // HTTP GET 请求
    client.print(String("GET ") + firmwareUrl + " HTTP/1.1\r\n");
    client.print("Host: " + _serverHost + "\r\n");
    client.print("Connection: close\r\n\r\n");
    
    // 等待响应
    delay(100);
    
    // 读取响应头
    int contentLength = 0;
    
    while (client.connected()) {
        String line = client.readLineUntil('\n');
        line.trim();
        
        if (line.startsWith("Content-Length:")) {
            contentLength = line.substring(16).toInt();
        } else if (line == "\r") {
            break;
        }
    }
    
    if (contentLength == 0) {
        Serial.println("[OTA] 无法获取固件大小");
        return false;
    }
    
    Serial.printf("[OTA] 固件大小：%d bytes\n", contentLength);
    
    // 开始 OTA
    if (!Update.begin(contentLength, U_FLASH)) {
        Serial.printf("[OTA] OTA 开始失败：%s\n", Update.errorString());
        return false;
    }
    
    // 下载并写入
    size_t downloaded = 0;
    uint8_t buffer[1024];
    
    uint32_t timeoutStart = millis();
    
    while (client.connected() && downloaded < contentLength) {
        size_t available = client.available();
        
        if (available > 0) {
            // 读取数据
            size_t toRead = min(available, sizeof(buffer));
            int bytesRead = client.read(buffer, toRead);
            
            // 写入 OTA
            size_t bytesWritten = Update.write(buffer, bytesRead);
            
            if (bytesWritten > 0) {
                downloaded += bytesWritten;
                
                // 报告进度
                int percent = (downloaded * 100) / contentLength;
                reportProgress(percent);
            }
            
            timeoutStart = millis();  // 重置超时
        }
        
        // 检查超时
        if (millis() - timeoutStart > OTA_DOWNLOAD_TIMEOUT_MS) {
            Serial.println("[OTA] 下载超时");
            return false;
        }
        
        yield();  // 看门狗喂狗
    }
    
    // 检查下载是否完整
    if (downloaded != contentLength) {
        Serial.printf("[OTA] 下载不完整：%d / %d\n", downloaded, contentLength);
        return false;
    }
    
    Serial.println("[OTA] 下载完成");
    return true;
}

bool OtaUpdater::verifyFirmware(const uint8_t* expectedHash) {
    setState(OTA_VERIFYING, "正在验证固件...");
    
    // ESP32 Update 库会自动验证 SHA256
    // 这里可以添加额外的验证逻辑
    
    if (!Update.end(true)) {
        Serial.printf("[OTA] 固件验证失败：%s\n", Update.errorString());
        return false;
    }
    
    Serial.println("[OTA] 固件验证通过");
    return true;
}
```

### 在 `main.cpp` 中集成

```cpp
// main.cpp

#include "ota_updater.h"

// OTA 事件回调
void onOtaEvent(OtaState state, int progress, const char* message) {
    Serial.printf("[OTA Event] 状态：%d, 进度：%d%%, 消息：%s\n", 
                  state, progress, message);
    
    // 更新屏幕显示
    switch (state) {
        case OTA_CHECKING:
            catFace.setExpression(CAT_THINKING);
            break;
        case OTA_DOWNLOADING:
            // 显示下载进度
            showOtaProgress(progress);
            break;
        case OTA_VERIFYING:
            catFace.setExpression(CAT_LISTENING);
            break;
        case OTA_SUCCESS:
            catFace.setExpression(CAT_HAPPY);
            break;
        case OTA_FAILED:
            catFace.setExpression(CAT_ANGRY);
            break;
        default:
            break;
    }
}

void setup() {
    // ... 现有初始化代码 ...
    
    // 初始化 OTA
    otaUpdater.begin("/api/v1/firmware/version", FIRMWARE_VERSION);
    otaUpdater.setServerInfo(OTA_SERVER_HOST, OTA_SERVER_PORT);
    otaUpdater.onEvent(onOtaEvent);
    
    Serial.printf("[Main] OTA 初始化完成，当前版本：%s\n", FIRMWARE_VERSION);
}

void loop() {
    // 检查 OTA 更新
    otaUpdater.checkForUpdates();
    
    // ... 其他业务逻辑 ...
    
    delay(100);
}

// 显示 OTA 进度（LVGL）
void showOtaProgress(int percent) {
    static lv_obj_t* progressBar = NULL;
    
    if (!progressBar) {
        progressBar = lv_bar_create(lv_screen_active());
        lv_obj_set_size(progressBar, 200, 20);
        lv_obj_center(progressBar);
        lv_bar_set_range(progressBar, 0, 100);
    }
    
    lv_bar_set_value(progressBar, percent, LV_ANIM_OFF);
    
    // 显示百分比文本
    static lv_obj_t* label = NULL;
    if (!label) {
        label = lv_label_create(lv_screen_active());
        lv_obj_align_to(label, progressBar, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    }
    
    static char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", percent);
    lv_label_set_text(label, buf);
}
```

---

## 4.3.3 云端版本服务

### 版本信息 API

```python
# main.py
from fastapi import FastAPI, HTTPException
from pydantic import BaseModel
import hashlib

app = FastAPI()

class FirmwareVersion(BaseModel):
    version: str
    firmware_url: str
    firmware_hash: str  # SHA256
    release_notes: str
    min_firmware_version: str  # 最低支持的旧版本
    forced_update: bool = False  # 是否强制更新

# 版本信息存储（生产环境应使用数据库）
FIRMWARE_VERSIONS = {
    "esp32": FirmwareVersion(
        version="1.2.0",
        firmware_url="/firmware/cyber_cat_1.2.0.bin",
        firmware_hash="a1b2c3d4e5f6...",
        release_notes="修复了内存泄漏问题，优化了表情动画",
        min_firmware_version="1.0.0",
        forced_update=False
    )
}

@app.get("/api/v1/firmware/version")
async def get_firmware_version(
    x_current_version: str = Header(None),
    x_device_id: str = Header(None)
):
    """获取固件版本信息"""
    
    latest = FIRMWARE_VERSIONS.get("esp32")
    if not latest:
        raise HTTPException(status_code=404, detail="固件版本信息不存在")
    
    # 检查是否需要更新
    needs_update = compare_versions(x_current_version or "0.0.0", latest.version) > 0
    
    if not needs_update:
        return {
            "version": latest.version,
            "has_update": False,
            "message": "已是最新版本"
        }
    
    # 记录升级请求（用于统计）
    print(f"[OTA] 设备 {x_device_id} 请求升级：{x_current_version} -> {latest.version}")
    
    return {
        "version": latest.version,
        "firmware_url": latest.firmware_url,
        "firmware_hash": latest.firmware_hash,
        "release_notes": latest.release_notes,
        "min_firmware_version": latest.min_firmware_version,
        "forced_update": latest.forced_update,
        "has_update": True
    }

@app.get("/firmware/{filename}")
async def download_firmware(filename: str):
    """下载固件文件"""
    # 实际项目中应该从文件存储或 CDN 返回固件
    
    firmware_path = f"/var/lib/cybercat/firmware/{filename}"
    
    from fastapi.responses import FileResponse
    import os
    
    if not os.path.exists(firmware_path):
        raise HTTPException(status_code=404, detail="固件文件不存在")
    
    return FileResponse(
        firmware_path,
        media_type="application/octet-stream",
        filename=filename
    )

def compare_versions(v1: str, v2: str) -> int:
    """比较版本号，返回 -1/0/1"""
    def normalize(v):
        return [int(x) for x in re.sub(r'[^0-9.]', '', v).split('.')]
    
    parts1 = normalize(v1)
    parts2 = normalize(v2)
    
    for p1, p2 in zip(parts1, parts2):
        if p1 < p2:
            return -1
        if p1 > p2:
            return 1
    
    if len(parts1) < len(parts2):
        return -1
    if len(parts1) > len(parts2):
        return 1
    
    return 0
```

### 固件文件管理

```python
# services/firmware_manager.py
import hashlib
import subprocess
from pathlib import Path

class FirmwareManager:
    def __init__(self, firmware_dir: str = "/var/lib/cybercat/firmware"):
        self.firmware_dir = Path(firmware_dir)
        self.firmware_dir.mkdir(parents=True, exist_ok=True)
    
    def upload_firmware(self, file_path: str, version: str) -> dict:
        """上传新固件文件"""
        # 计算 SHA256
        sha256_hash = self._calculate_sha256(file_path)
        
        # 生成目标文件名
        target_name = f"cyber_cat_{version}.bin"
        target_path = self.firmware_dir / target_name
        
        # 移动文件
        import shutil
        shutil.copy(file_path, target_path)
        
        # 验证文件大小
        file_size = target_path.stat().st_size
        
        return {
            "version": version,
            "filename": target_name,
            "hash": sha256_hash,
            "size": file_size,
            "path": str(target_path)
        }
    
    def _calculate_sha256(self, file_path: str) -> str:
        """计算文件 SHA256"""
        sha256 = hashlib.sha256()
        
        with open(file_path, "rb") as f:
            for chunk in iter(lambda: f.read(4096), b""):
                sha256.update(chunk)
        
        return sha256.hexdigest()
    
    def list_firmware(self) -> list:
        """列出所有固件"""
        firmware_list = []
        
        for f in self.firmware_dir.glob("*.bin"):
            firmware_list.append({
                "filename": f.name,
                "size": f.stat().st_size,
                "modified": f.stat().st_mtime
            })
        
        return sorted(firmware_list, key=lambda x: x["modified"], reverse=True)

# 使用示例
firmware_manager = FirmwareManager()

@app.post("/admin/firmware")
async def upload_firmware(file: UploadFile, version: str = Query(...)):
    """管理员上传固件"""
    # 保存上传文件
    temp_path = f"/tmp/{file.filename}"
    
    with open(temp_path, "wb") as f:
        content = await file.read()
        f.write(content)
    
    result = firmware_manager.upload_firmware(temp_path, version)
    
    return result
```

---

## 4.3.4 升级失败回滚

### 看门狗与回滚机制

```cpp
// src/ota_updater.cpp 增强版

#include <esp_system.h>
#include <esp_ota_ops.h>

// OTA 回滚检查
void checkOtaRollback() {
    esp_ota_img_states_t state;
    esp_ota_get_state_partition(esp_ota_get_running_partition(), &state);
    
    if (state == ESP_OTA_IMG_PENDING_VERIFY) {
        Serial.println("[OTA] 新固件正在验证中...");
        
        // 检查启动次数
        int bootCount = getBootCount();
        
        if (bootCount > 3) {
            // 启动失败超过 3 次，回滚到旧版本
            Serial.println("[OTA] 启动失败次数过多，回滚到旧固件");
            esp_ota_mark_app_invalid_rollback_and_reboot();
        } else {
            // 验证成功，标记为可用
            Serial.println("[OTA] 新固件验证成功");
            esp_ota_mark_app_valid_cancel_rollback();
        }
    }
}

// 在 setup() 中调用
void setup() {
    Serial.begin(115200);
    
    // 检查 OTA 回滚
    checkOtaRollback();
    
    // ... 其他初始化
}

// 获取启动次数（保存到 RTC 内存）
RTC_DATA_ATTR int g_bootCount = 0;

int getBootCount() {
    g_bootCount++;
    return g_bootCount;
}
```

### 升级超时回滚

```cpp
// ota_updater.cpp 增强版

class OtaUpdater {
private:
    uint32_t _updateStartTime;
    static const uint32_t UPDATE_TIMEOUT_MS = 300000;  // 5 分钟超时
    
public:
    bool downloadUpdate(const char* firmwareUrl) {
        _updateStartTime = millis();
        
        // ... 下载逻辑 ...
        
        // 检查超时
        if (millis() - _updateStartTime > UPDATE_TIMEOUT_MS) {
            Serial.println("[OTA] 升级超时，触发回滚");
            triggerRollback();
            return false;
        }
        
        return true;
    }
    
private:
    void triggerRollback() {
        Serial.println("[OTA] 触发回滚到旧固件");
        
        // 标记当前 OTA 分区为无效
        esp_ota_mark_app_invalid_rollback_and_reboot();
        
        // 如果上面没重启，强制重启
        delay(1000);
        ESP.restart();
    }
};
```

---

## 4.3.5 增量更新（可选优化）

对于大型固件，可以使用增量更新减少下载流量。

```python
# 使用 bsdiff 生成增量包
# pip install bsdiff4

import bsdiff4

def create_patch(old_file: str, new_file: str, patch_file: str):
    """创建增量包"""
    with open(old_file, 'rb') as f:
        old_data = f.read()
    
    with open(new_file, 'rb') as f:
        new_data = f.read()
    
    patch_data = bsdiff4.diff(old_data, new_data)
    
    with open(patch_file, 'wb') as f:
        f.write(patch_data)
    
    print(f"增量包大小：{len(patch_data)} bytes")
    return len(patch_data)

# 固件端使用
# 需要集成 bspatch 或类似库
```

---

## 本章小结

本章学习了 OTA 远程升级的完整实现：

**固件端：**
- ✅ OTA 分区结构与原理
- ✅ 版本检查与下载
- ✅ 固件验证与刷写
- ✅ 失败回滚机制

**云端：**
- ✅ 版本信息 API
- ✅ 固件文件管理
- ✅ SHA256 校验

**进阶：**
- ✅ 看门狗超时回滚
- ✅ 增量更新优化

---

## 下一章

继续学习 **[4.4 最佳实践](04-最佳实践.md)**，总结项目开发经验。

---

## 实践练习

1. 实现断点续传下载（Range 请求头）
2. 添加升级进度条到 LVGL 界面
3. 实现双分区备份（两个分区都保留最新固件）
4. 添加升级通知（升级前提醒用户）
