#include "LVGL_Driver.h"

uint32_t bufSize;
lv_color_t *disp_draw_buf1;
lv_color_t *disp_draw_buf2;

static StaticSemaphore_t lvgl_mutex_buf;
static SemaphoreHandle_t lvgl_mutex = NULL;

static void lvgl_port_lock_init(void) {
  if (lvgl_mutex == NULL) {
    lvgl_mutex = xSemaphoreCreateRecursiveMutexStatic(&lvgl_mutex_buf);
    if (lvgl_mutex == NULL) {
      Serial.println("Failed to create LVGL recursive mutex!");
    }
  }
}

bool lvgl_port_lock(uint32_t timeout_ms) {
  if (lvgl_mutex == NULL) return false;
  TickType_t timeout_ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
  if (xSemaphoreTakeRecursive(lvgl_mutex, timeout_ticks) == pdTRUE) {
    return true;
  } else {
    return false;
  }
}

void lvgl_port_unlock(void) {
  if (lvgl_mutex != NULL) {
    xSemaphoreGiveRecursive(lvgl_mutex);
  }
}

static volatile uint32_t _lvglLoopCount = 0;
static volatile uint32_t _frameCount    = 0;
static volatile uint32_t _renderMaxMs   = 0;

uint32_t lvgl_get_loop_count(void) { return _lvglLoopCount; }
uint32_t lvgl_get_frame_count(void) { return _frameCount; }

uint32_t lvgl_take_render_max_ms(void) {
  uint32_t v = _renderMaxMs;
  _renderMaxMs = 0;
  return v;
}

static volatile bool    _touchPressed = false;
static volatile int16_t _touchX = 0, _touchY = 0;

bool lvgl_touch_pressed(int16_t *x, int16_t *y) {
  if (x) *x = _touchX;
  if (y) *y = _touchY;
  return _touchPressed;
}

// The SPI master copies non-DMA-capable (PSRAM) buffers into internal RAM when
// the transaction is queued, so the LVGL buffer can be released immediately.
static void my_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
  lv_draw_sw_rgb565_swap(px_map, lv_area_get_size(area));
  LCD_addWindow(area->x1, area->y1, area->x2, area->y2, (uint16_t *)px_map);
  if (lv_display_flush_is_last(disp)) _frameCount++;
  lv_display_flush_ready(disp);
}

static void my_touchpad_read(lv_indev_t *indev_drv, lv_indev_data_t *data)
{
  Touch_Read_Data();
  if (touch_data.points != 0x00) {
    data->point.x = touch_data.x;
    data->point.y = touch_data.y;
    data->state = LV_INDEV_STATE_PR;
    _touchX = touch_data.x;
    _touchY = touch_data.y;
    _touchPressed = true;
  } else {
    data->state = LV_INDEV_STATE_REL;
    _touchPressed = false;
  }
  touch_data.x = 0;
  touch_data.y = 0;
  touch_data.points = 0;
}

static uint32_t my_tick(void) {
  return millis();
}

// ---- Update callback: called every cycle BEFORE lv_timer_handler ----
static lvgl_update_cb_t _update_cb = NULL;

void lvgl_set_update_cb(lvgl_update_cb_t cb) {
  _update_cb = cb;
}

static void LVGL_Loop(void *parameter) {
  while (1) {
    lvgl_port_lock(0);

    if (_update_cb) _update_cb();

    uint32_t t0 = millis();
    uint32_t untilNext = lv_timer_handler();
    uint32_t dt = millis() - t0;
    if (dt > _renderMaxMs) _renderMaxMs = dt;
    if (dt > 2000) {
      Serial.printf("[LVGL] WARN: lv_timer_handler stuck for %u ms!\n", (unsigned)dt);
    }

    _lvglLoopCount++;
    lvgl_port_unlock();
    // Sleep only until the next LVGL timer is due so a 16ms refresh period is
    // not stretched to 16ms + render time; always yield at least one tick.
    if (untilNext > 16) untilNext = 16;
    if (untilNext < 1) untilNext = 1;
    vTaskDelay(pdMS_TO_TICKS(untilNext));
  }
  vTaskDelete(NULL);
}

void Lvgl_Init(void) {
  lvgl_port_lock_init();

  // Partial render: 1/10 screen stripe (~26 KB each) instead of full frame (259 KB each)
  bufSize = LVGL_BUF_LEN;
  disp_draw_buf1 = (lv_color_t *)heap_caps_malloc(bufSize * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  disp_draw_buf2 = (lv_color_t *)heap_caps_malloc(bufSize * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

  lv_init();
  lv_tick_set_cb(my_tick);

  lv_display_t *disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
  lv_display_set_flush_cb(disp, my_disp_flush);
  lv_display_set_buffers(disp, disp_draw_buf1, disp_draw_buf2, bufSize * 2, LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, my_touchpad_read);

  xTaskCreatePinnedToCore(LVGL_Loop, "LVGL Loop", 16384, NULL, 12, NULL, 0);
}
