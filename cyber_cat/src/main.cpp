#include <Arduino.h>
#include <WiFi.h>
#include <driver/i2s.h>

#include "I2C_Driver.h"
#include "TCA9554PWR.h"
#include "Display_ST77916.h"
#include "LVGL_Driver.h"
#include "cat_face.h"

// ============================================================
//  WiFi 配置 —— 改成你自己的 WiFi 名称和密码
// ============================================================
const char *WIFI_SSID = "MyPhone";
const char *WIFI_PASS = "12346789";

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
static lv_obj_t *wifi_dot = NULL;

// ============================================================
//  WiFi 连接
// ============================================================
static void wifi_connect() {
    Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    int timeout = 60;
    while (WiFi.status() != WL_CONNECTED && timeout > 0) {
        delay(500);
        Serial.print(".");
        timeout--;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\nWiFi connected! IP: %s\n",
                      WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\nWiFi connection failed. Continuing without WiFi.");
    }
}

// ============================================================
//  I2S 麦克风初始化
// ============================================================
static void mic_i2s_init() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = 16000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = 512,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0,
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = MIC_SCK_PIN,
        .ws_io_num = MIC_WS_PIN,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = MIC_SD_PIN,
    };

    esp_err_t err = i2s_driver_install(MIC_I2S_PORT, &i2s_config, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("MIC I2S driver install failed: %d\n", err);
        return;
    }
    i2s_set_pin(MIC_I2S_PORT, &pin_config);
    Serial.println("MIC I2S initialized (16kHz, 16bit)");
}

// ============================================================
//  I2S 喇叭初始化
// ============================================================
static void spk_i2s_init() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = 16000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = 512,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0,
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = SPK_BCK_PIN,
        .ws_io_num = SPK_LRCK_PIN,
        .data_out_num = SPK_DIN_PIN,
        .data_in_num = I2S_PIN_NO_CHANGE,
    };

    esp_err_t err = i2s_driver_install(SPK_I2S_PORT, &i2s_config, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("SPK I2S driver install failed: %d\n", err);
        return;
    }
    i2s_set_pin(SPK_I2S_PORT, &pin_config);
    Serial.println("SPK I2S initialized (PCM5101)");
}

// ============================================================
//  播放一段简单的提示音（正弦波 beep）
// ============================================================
static void play_beep() {
    const int sample_rate = 16000;
    const int freq = 800;
    const int duration_ms = 300;
    const int num_samples = sample_rate * duration_ms / 1000;

    int16_t *buf = (int16_t *)malloc(num_samples * 2 * sizeof(int16_t));
    if (!buf) return;

    for (int i = 0; i < num_samples; i++) {
        int16_t sample =
            (int16_t)(16000.0 * sin(2.0 * M_PI * freq * i / sample_rate));
        buf[i * 2] = sample;
        buf[i * 2 + 1] = sample;
    }

    size_t bytes_written = 0;
    i2s_write(SPK_I2S_PORT, buf, num_samples * 2 * sizeof(int16_t),
              &bytes_written, portMAX_DELAY);
    free(buf);

    Serial.printf("Beep played! (%d bytes)\n", bytes_written);
}

// ============================================================
//  Arduino setup()
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(1000);
    randomSeed(micros());

    Serial.println("====================================");
    Serial.println("  Cyber Cat - Phase 2 Cat Face");
    Serial.println("====================================");

    // 1. I2C + GPIO 扩展器
    Serial.println("[1/6] I2C & TCA9554PWR...");
    I2C_Init();
    TCA9554PWR_Init(0x00);

    // 2. 背光 + LCD + 触摸
    Serial.println("[2/6] LCD & Touch...");
    Backlight_Init();
    LCD_Init();

    // 3. LVGL
    Serial.println("[3/6] LVGL...");
    Lvgl_Init();

    // 4. 猫脸 UI
    Serial.println("[4/6] Cat Face...");
    lvgl_port_lock(0);
    catFace.create(lv_screen_active());

    wifi_dot = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(wifi_dot);
    lv_obj_set_size(wifi_dot, 10, 10);
    lv_obj_set_style_bg_color(wifi_dot, lv_color_hex(0xe94560), 0);
    lv_obj_set_style_bg_opa(wifi_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(wifi_dot, 5, 0);
    lv_obj_remove_flag(wifi_dot, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_align(wifi_dot, LV_ALIGN_TOP_MID, 0, 25);
    lvgl_port_unlock();

    // 5. WiFi
    Serial.println("[5/6] WiFi...");
    wifi_connect();

    lvgl_port_lock(0);
    if (WiFi.status() == WL_CONNECTED) {
        lv_obj_set_style_bg_color(wifi_dot, lv_color_hex(0x16c79a), 0);
    }
    lvgl_port_unlock();

    // 6. I2S 音频
    Serial.println("[6/6] I2S Audio...");
    // Phase 2 不需要音频，全部关闭避免底噪
    // mic_i2s_init();  — Phase 4 再启用
    // spk_i2s_init();  — Phase 4 再启用

    // 主动拉低所有音频 I2S 引脚，防止浮空噪声
    const int audioPins[] = {
        SPK_DIN_PIN, SPK_LRCK_PIN, SPK_BCK_PIN,
        MIC_WS_PIN, MIC_SCK_PIN, MIC_SD_PIN
    };
    for (int pin : audioPins) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
    }
    Serial.println("Audio pins all LOW (muted)");

    Serial.println("====================================");
    Serial.println("  Cat Face ready!");
    Serial.println("  Tap   -> Happy (2s)");
    Serial.println("  Swipe -> Cycle expressions");
    Serial.println("====================================");
}

// ============================================================
//  Arduino loop()
// ============================================================
void loop() {
    lvgl_port_lock(0);

    catFace.update();

    // WiFi 状态小圆点
    static bool lastWifi = false;
    bool connected = (WiFi.status() == WL_CONNECTED);
    if (connected != lastWifi) {
        lv_obj_set_style_bg_color(
            wifi_dot,
            connected ? lv_color_hex(0x16c79a) : lv_color_hex(0xe94560), 0);
        lastWifi = connected;
    }

    lvgl_port_unlock();
    delay(30);
}
