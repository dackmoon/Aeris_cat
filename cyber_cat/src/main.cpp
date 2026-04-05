#include <Arduino.h>
#include <driver/i2s.h>

#include "I2C_Driver.h"
#include "TCA9554PWR.h"
#include "Display_ST77916.h"
#include "LVGL_Driver.h"
#include "cat_face.h"
#include "wifi_manager.h"
#include "cloud_client.h"
#include "secrets.h"

// ============================================================
//  I2S 音频引脚定义（板载硬件）
// ============================================================
#define MIC_I2S_PORT   I2S_NUM_0
#define MIC_WS_PIN     2
#define MIC_SCK_PIN    15
#define MIC_SD_PIN     39

#define SPK_I2S_PORT   I2S_NUM_1
#define SPK_DIN_PIN    47
#define SPK_LRCK_PIN   38
#define SPK_BCK_PIN    48

// WiFi 状态小圆点
static lv_obj_t *wifi_dot = nullptr;

// 云端客户端
static CloudClient cloud;

// ============================================================
//  长按回调 — 触发 AI 对话
// ============================================================
static void onLongPress() {
    if (cloud.poll() == CLOUD_BUSY) {
        Serial.println("[Main] Cloud busy, ignoring long press");
        return;
    }
    if (!wifiConnected()) {
        // No network — cat doesn't know why, just looks puzzled
        Serial.println("[Main] No WiFi, showing Curious");
        catFace.setExpression(CAT_CURIOUS);
        return;
    }

    Serial.println("[Main] Long press -> sending chat");
    catFace.setExpression(CAT_TALKING);
    cloud.sendChat("你好呀，今天过得怎么样？");
}

// ============================================================
//  Arduino setup()
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(1000);
    randomSeed(micros());

    Serial.println("====================================");
    Serial.println("  Cyber Cat - Phase 3 Cloud");
    Serial.println("====================================");

    // 1. I2C + GPIO 扩展器
    Serial.println("[1/5] I2C & TCA9554PWR...");
    I2C_Init();
    TCA9554PWR_Init(0x00);

    // 2. 背光 + LCD + 触摸
    Serial.println("[2/5] LCD & Touch...");
    Backlight_Init();
    LCD_Init();

    // 3. LVGL
    Serial.println("[3/5] LVGL...");
    Lvgl_Init();

    // 4. 猫脸 UI
    Serial.println("[4/5] Cat Face...");
    lvgl_port_lock(0);
    catFace.create(lv_screen_active());
    catFace.onLongPress(onLongPress);

    wifi_dot = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(wifi_dot);
    lv_obj_set_size(wifi_dot, 10, 10);
    lv_obj_set_style_bg_color(wifi_dot, lv_color_hex(0xe94560), 0);
    lv_obj_set_style_bg_opa(wifi_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(wifi_dot, 5, 0);
    lv_obj_remove_flag(wifi_dot,
        (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_align(wifi_dot, LV_ALIGN_TOP_MID, 0, 25);
    lvgl_port_unlock();

    // 5. WiFi + Cloud
    Serial.println("[5/5] WiFi & Cloud...");
    wifiInit(WIFI_SSID, WIFI_PASS);
    cloud.begin(LLM_API_KEY, LLM_API_BASE, LLM_MODEL);
    // wifi_dot color is updated reactively in loop() via wifiLoop() event

    // 音频引脚拉低（Phase 4 再启用）
    const int audioPins[] = {
        SPK_DIN_PIN, SPK_LRCK_PIN, SPK_BCK_PIN,
        MIC_WS_PIN, MIC_SCK_PIN, MIC_SD_PIN
    };
    for (int pin : audioPins) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
    }

    Serial.println("====================================");
    Serial.println("  Cat ready!");
    Serial.println("  Tap       -> Happy (2s)");
    Serial.println("  Long press-> Chat with AI");
    Serial.println("  Swipe     -> Cycle expressions");
    Serial.println("====================================");
}

// ============================================================
//  Arduino loop()
// ============================================================
void loop() {
    wifiLoop();

    lvgl_port_lock(0);

    catFace.update();

    // WiFi 状态指示灯
    static bool lastWifi = false;
    bool connected = wifiConnected();
    if (connected != lastWifi) {
        lv_obj_set_style_bg_color(
            wifi_dot,
            connected ? lv_color_hex(0x16c79a) : lv_color_hex(0xe94560), 0);
        lastWifi = connected;
    }

    // 轮询云端响应
    CloudState cs = cloud.poll();
    if (cs == CLOUD_DONE) {
        Serial.printf("[Main] AI says: %s\n", cloud.getResponse());
        // Return to Neutral — emotion will be driven by AI sentiment in Phase 5
        catFace.setExpression(CAT_NEUTRAL);
        cloud.consume();
    } else if (cs == CLOUD_ERROR) {
        Serial.printf("[Main] Cloud error: %s\n", cloud.getError());
        // Cat doesn't understand what went wrong, just goes back to idle
        catFace.setExpression(CAT_NEUTRAL);
        cloud.consume();
    }

    lvgl_port_unlock();
    delay(30);
}
