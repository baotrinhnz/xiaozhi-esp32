#include "application.h"
#include "backlight.h"
#include "button.h"
#include "codecs/box_audio_codec.h"
#include "config.h"
#include "display/emote_display.h"
#include "display/lcd_display.h"
#include "gfx.h"                        // gfx_label_* / gfx_obj_* cho màn media native (tên cuộn + giờ)
#include "mcp_server.h"                 // đăng ký tool MCP self.screen.show_panel (brain ra lệnh hiện panel)

// Font tiếng Việt (LVGL v9, nhúng từ vocat_vn_26.c cùng thư mục board) cho màn media native.
LV_FONT_DECLARE(vocat_vn_26);
#include "esp_video.h"
#include "wifi_board.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <esp_http_client.h>
#include <esp_mac.h>
#include <cstdio>
#include <cinttypes>
#include "esp_idf_version.h"

#define ESP_VOCAT_ENABLE_CAP_TOUCH_SENSOR (ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6, 0, 0))

#include <driver/i2c_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_st77916.h>
#include <cstdlib>
#include "bmi270_api.h"
#include "esp_lcd_touch_cst816s.h"
#include "i2c_bus.h"
#include "i2c_device.h"
#include "touch.h"

#if ESP_VOCAT_ENABLE_CAP_TOUCH_SENSOR
extern "C" {
#include "touch_button_sensor.h"
#include "touch_slider_sensor.h"
}
#endif

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "driver/temperature_sensor.h"

#define TAG "ESP-VoCat"

namespace Bmi270Motion {
static bmi270_handle_t bmi_handle_ = nullptr;

esp_err_t Initialize(i2c_bus_handle_t i2c_bus) {
    if (bmi_handle_) {
        return ESP_OK;
    }
    if (!i2c_bus) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = bmi270_sensor_create(i2c_bus, &bmi_handle_, bmi270_config_file,
                                         BMI2_GYRO_CROSS_SENS_ENABLE | BMI2_CRT_RTOSK_ENABLE);
    if (ret != ESP_OK || !bmi_handle_) {
        ESP_LOGW(TAG, "BMI270 init failed: %s", esp_err_to_name(ret));
        return ret == ESP_OK ? ESP_FAIL : ret;
    }

    const uint8_t sens_list[] = {BMI2_ACCEL};
    int8_t rslt = bmi270_sensor_enable(sens_list, 1, bmi_handle_);
    if (rslt != BMI2_OK) {
        ESP_LOGW(TAG, "BMI270 accel enable failed: %d", rslt);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "BMI270 initialized");
    return ESP_OK;
}

bool ReadAccelRaw(struct bmi2_sens_data& accel) {
    if (!bmi_handle_) {
        return false;
    }
    int8_t rslt = bmi2_get_sensor_data(&accel, bmi_handle_);
    return rslt == BMI2_OK;
}
}  // namespace Bmi270Motion

temperature_sensor_handle_t temp_sensor = NULL;
static const st77916_lcd_init_cmd_t vendor_specific_init_yysj[] = {
    {0xF0, (uint8_t[]){0x28}, 1, 0},
    {0xF2, (uint8_t[]){0x28}, 1, 0},
    {0x73, (uint8_t[]){0xF0}, 1, 0},
    {0x7C, (uint8_t[]){0xD1}, 1, 0},
    {0x83, (uint8_t[]){0xE0}, 1, 0},
    {0x84, (uint8_t[]){0x61}, 1, 0},
    {0xF2, (uint8_t[]){0x82}, 1, 0},
    {0xF0, (uint8_t[]){0x00}, 1, 0},
    {0xF0, (uint8_t[]){0x01}, 1, 0},
    {0xF1, (uint8_t[]){0x01}, 1, 0},
    {0xB0, (uint8_t[]){0x56}, 1, 0},
    {0xB1, (uint8_t[]){0x4D}, 1, 0},
    {0xB2, (uint8_t[]){0x24}, 1, 0},
    {0xB4, (uint8_t[]){0x87}, 1, 0},
    {0xB5, (uint8_t[]){0x44}, 1, 0},
    {0xB6, (uint8_t[]){0x8B}, 1, 0},
    {0xB7, (uint8_t[]){0x40}, 1, 0},
    {0xB8, (uint8_t[]){0x86}, 1, 0},
    {0xBA, (uint8_t[]){0x00}, 1, 0},
    {0xBB, (uint8_t[]){0x08}, 1, 0},
    {0xBC, (uint8_t[]){0x08}, 1, 0},
    {0xBD, (uint8_t[]){0x00}, 1, 0},
    {0xC0, (uint8_t[]){0x80}, 1, 0},
    {0xC1, (uint8_t[]){0x10}, 1, 0},
    {0xC2, (uint8_t[]){0x37}, 1, 0},
    {0xC3, (uint8_t[]){0x80}, 1, 0},
    {0xC4, (uint8_t[]){0x10}, 1, 0},
    {0xC5, (uint8_t[]){0x37}, 1, 0},
    {0xC6, (uint8_t[]){0xA9}, 1, 0},
    {0xC7, (uint8_t[]){0x41}, 1, 0},
    {0xC8, (uint8_t[]){0x01}, 1, 0},
    {0xC9, (uint8_t[]){0xA9}, 1, 0},
    {0xCA, (uint8_t[]){0x41}, 1, 0},
    {0xCB, (uint8_t[]){0x01}, 1, 0},
    {0xD0, (uint8_t[]){0x91}, 1, 0},
    {0xD1, (uint8_t[]){0x68}, 1, 0},
    {0xD2, (uint8_t[]){0x68}, 1, 0},
    {0xF5, (uint8_t[]){0x00, 0xA5}, 2, 0},
    {0xDD, (uint8_t[]){0x4F}, 1, 0},
    {0xDE, (uint8_t[]){0x4F}, 1, 0},
    {0xF1, (uint8_t[]){0x10}, 1, 0},
    {0xF0, (uint8_t[]){0x00}, 1, 0},
    {0xF0, (uint8_t[]){0x02}, 1, 0},
    {0xE0,
     (uint8_t[]){0xF0, 0x0A, 0x10, 0x09, 0x09, 0x36, 0x35, 0x33, 0x4A, 0x29, 0x15, 0x15, 0x2E,
                 0x34},
     14, 0},
    {0xE1,
     (uint8_t[]){0xF0, 0x0A, 0x0F, 0x08, 0x08, 0x05, 0x34, 0x33, 0x4A, 0x39, 0x15, 0x15, 0x2D,
                 0x33},
     14, 0},
    {0xF0, (uint8_t[]){0x10}, 1, 0},
    {0xF3, (uint8_t[]){0x10}, 1, 0},
    {0xE0, (uint8_t[]){0x07}, 1, 0},
    {0xE1, (uint8_t[]){0x00}, 1, 0},
    {0xE2, (uint8_t[]){0x00}, 1, 0},
    {0xE3, (uint8_t[]){0x00}, 1, 0},
    {0xE4, (uint8_t[]){0xE0}, 1, 0},
    {0xE5, (uint8_t[]){0x06}, 1, 0},
    {0xE6, (uint8_t[]){0x21}, 1, 0},
    {0xE7, (uint8_t[]){0x01}, 1, 0},
    {0xE8, (uint8_t[]){0x05}, 1, 0},
    {0xE9, (uint8_t[]){0x02}, 1, 0},
    {0xEA, (uint8_t[]){0xDA}, 1, 0},
    {0xEB, (uint8_t[]){0x00}, 1, 0},
    {0xEC, (uint8_t[]){0x00}, 1, 0},
    {0xED, (uint8_t[]){0x0F}, 1, 0},
    {0xEE, (uint8_t[]){0x00}, 1, 0},
    {0xEF, (uint8_t[]){0x00}, 1, 0},
    {0xF8, (uint8_t[]){0x00}, 1, 0},
    {0xF9, (uint8_t[]){0x00}, 1, 0},
    {0xFA, (uint8_t[]){0x00}, 1, 0},
    {0xFB, (uint8_t[]){0x00}, 1, 0},
    {0xFC, (uint8_t[]){0x00}, 1, 0},
    {0xFD, (uint8_t[]){0x00}, 1, 0},
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xFF, (uint8_t[]){0x00}, 1, 0},
    {0x60, (uint8_t[]){0x40}, 1, 0},
    {0x61, (uint8_t[]){0x04}, 1, 0},
    {0x62, (uint8_t[]){0x00}, 1, 0},
    {0x63, (uint8_t[]){0x42}, 1, 0},
    {0x64, (uint8_t[]){0xD9}, 1, 0},
    {0x65, (uint8_t[]){0x00}, 1, 0},
    {0x66, (uint8_t[]){0x00}, 1, 0},
    {0x67, (uint8_t[]){0x00}, 1, 0},
    {0x68, (uint8_t[]){0x00}, 1, 0},
    {0x69, (uint8_t[]){0x00}, 1, 0},
    {0x6A, (uint8_t[]){0x00}, 1, 0},
    {0x6B, (uint8_t[]){0x00}, 1, 0},
    {0x70, (uint8_t[]){0x40}, 1, 0},
    {0x71, (uint8_t[]){0x03}, 1, 0},
    {0x72, (uint8_t[]){0x00}, 1, 0},
    {0x73, (uint8_t[]){0x42}, 1, 0},
    {0x74, (uint8_t[]){0xD8}, 1, 0},
    {0x75, (uint8_t[]){0x00}, 1, 0},
    {0x76, (uint8_t[]){0x00}, 1, 0},
    {0x77, (uint8_t[]){0x00}, 1, 0},
    {0x78, (uint8_t[]){0x00}, 1, 0},
    {0x79, (uint8_t[]){0x00}, 1, 0},
    {0x7A, (uint8_t[]){0x00}, 1, 0},
    {0x7B, (uint8_t[]){0x00}, 1, 0},
    {0x80, (uint8_t[]){0x48}, 1, 0},
    {0x81, (uint8_t[]){0x00}, 1, 0},
    {0x82, (uint8_t[]){0x06}, 1, 0},
    {0x83, (uint8_t[]){0x02}, 1, 0},
    {0x84, (uint8_t[]){0xD6}, 1, 0},
    {0x85, (uint8_t[]){0x04}, 1, 0},
    {0x86, (uint8_t[]){0x00}, 1, 0},
    {0x87, (uint8_t[]){0x00}, 1, 0},
    {0x88, (uint8_t[]){0x48}, 1, 0},
    {0x89, (uint8_t[]){0x00}, 1, 0},
    {0x8A, (uint8_t[]){0x08}, 1, 0},
    {0x8B, (uint8_t[]){0x02}, 1, 0},
    {0x8C, (uint8_t[]){0xD8}, 1, 0},
    {0x8D, (uint8_t[]){0x04}, 1, 0},
    {0x8E, (uint8_t[]){0x00}, 1, 0},
    {0x8F, (uint8_t[]){0x00}, 1, 0},
    {0x90, (uint8_t[]){0x48}, 1, 0},
    {0x91, (uint8_t[]){0x00}, 1, 0},
    {0x92, (uint8_t[]){0x0A}, 1, 0},
    {0x93, (uint8_t[]){0x02}, 1, 0},
    {0x94, (uint8_t[]){0xDA}, 1, 0},
    {0x95, (uint8_t[]){0x04}, 1, 0},
    {0x96, (uint8_t[]){0x00}, 1, 0},
    {0x97, (uint8_t[]){0x00}, 1, 0},
    {0x98, (uint8_t[]){0x48}, 1, 0},
    {0x99, (uint8_t[]){0x00}, 1, 0},
    {0x9A, (uint8_t[]){0x0C}, 1, 0},
    {0x9B, (uint8_t[]){0x02}, 1, 0},
    {0x9C, (uint8_t[]){0xDC}, 1, 0},
    {0x9D, (uint8_t[]){0x04}, 1, 0},
    {0x9E, (uint8_t[]){0x00}, 1, 0},
    {0x9F, (uint8_t[]){0x00}, 1, 0},
    {0xA0, (uint8_t[]){0x48}, 1, 0},
    {0xA1, (uint8_t[]){0x00}, 1, 0},
    {0xA2, (uint8_t[]){0x05}, 1, 0},
    {0xA3, (uint8_t[]){0x02}, 1, 0},
    {0xA4, (uint8_t[]){0xD5}, 1, 0},
    {0xA5, (uint8_t[]){0x04}, 1, 0},
    {0xA6, (uint8_t[]){0x00}, 1, 0},
    {0xA7, (uint8_t[]){0x00}, 1, 0},
    {0xA8, (uint8_t[]){0x48}, 1, 0},
    {0xA9, (uint8_t[]){0x00}, 1, 0},
    {0xAA, (uint8_t[]){0x07}, 1, 0},
    {0xAB, (uint8_t[]){0x02}, 1, 0},
    {0xAC, (uint8_t[]){0xD7}, 1, 0},
    {0xAD, (uint8_t[]){0x04}, 1, 0},
    {0xAE, (uint8_t[]){0x00}, 1, 0},
    {0xAF, (uint8_t[]){0x00}, 1, 0},
    {0xB0, (uint8_t[]){0x48}, 1, 0},
    {0xB1, (uint8_t[]){0x00}, 1, 0},
    {0xB2, (uint8_t[]){0x09}, 1, 0},
    {0xB3, (uint8_t[]){0x02}, 1, 0},
    {0xB4, (uint8_t[]){0xD9}, 1, 0},
    {0xB5, (uint8_t[]){0x04}, 1, 0},
    {0xB6, (uint8_t[]){0x00}, 1, 0},
    {0xB7, (uint8_t[]){0x00}, 1, 0},
    {0xB8, (uint8_t[]){0x48}, 1, 0},
    {0xB9, (uint8_t[]){0x00}, 1, 0},
    {0xBA, (uint8_t[]){0x0B}, 1, 0},
    {0xBB, (uint8_t[]){0x02}, 1, 0},
    {0xBC, (uint8_t[]){0xDB}, 1, 0},
    {0xBD, (uint8_t[]){0x04}, 1, 0},
    {0xBE, (uint8_t[]){0x00}, 1, 0},
    {0xBF, (uint8_t[]){0x00}, 1, 0},
    {0xC0, (uint8_t[]){0x10}, 1, 0},
    {0xC1, (uint8_t[]){0x47}, 1, 0},
    {0xC2, (uint8_t[]){0x56}, 1, 0},
    {0xC3, (uint8_t[]){0x65}, 1, 0},
    {0xC4, (uint8_t[]){0x74}, 1, 0},
    {0xC5, (uint8_t[]){0x88}, 1, 0},
    {0xC6, (uint8_t[]){0x99}, 1, 0},
    {0xC7, (uint8_t[]){0x01}, 1, 0},
    {0xC8, (uint8_t[]){0xBB}, 1, 0},
    {0xC9, (uint8_t[]){0xAA}, 1, 0},
    {0xD0, (uint8_t[]){0x10}, 1, 0},
    {0xD1, (uint8_t[]){0x47}, 1, 0},
    {0xD2, (uint8_t[]){0x56}, 1, 0},
    {0xD3, (uint8_t[]){0x65}, 1, 0},
    {0xD4, (uint8_t[]){0x74}, 1, 0},
    {0xD5, (uint8_t[]){0x88}, 1, 0},
    {0xD6, (uint8_t[]){0x99}, 1, 0},
    {0xD7, (uint8_t[]){0x01}, 1, 0},
    {0xD8, (uint8_t[]){0xBB}, 1, 0},
    {0xD9, (uint8_t[]){0xAA}, 1, 0},
    {0xF3, (uint8_t[]){0x01}, 1, 0},
    {0xF0, (uint8_t[]){0x00}, 1, 0},
    {0x21, (uint8_t[]){}, 0, 0},
    {0x11, (uint8_t[]){}, 0, 0},
    {0x00, (uint8_t[]){}, 0, 120},
};
float tsens_value;
gpio_num_t AUDIO_I2S_GPIO_DIN = AUDIO_I2S_GPIO_DIN_1;
gpio_num_t AUDIO_CODEC_PA_PIN = AUDIO_CODEC_PA_PIN_1;
gpio_num_t QSPI_PIN_NUM_LCD_RST = QSPI_PIN_NUM_LCD_RST_1;
gpio_num_t TOUCH_PAD2 = TOUCH_PAD2_1;
gpio_num_t UART1_TX = UART1_TX_1;
gpio_num_t UART1_RX = UART1_RX_1;

class EspVocat;

class Charge : public I2cDevice {
public:
    static constexpr uint8_t kRegVoltage = 0x08;
    static constexpr uint8_t kRegBatteryStatus = 0x0A;
    static constexpr uint8_t kRegCurrent = 0x0C;
    static constexpr uint8_t kRegStateOfCharge = 0x2C;
    static constexpr uint8_t kRegAverageCurrent = 0x14;
    static constexpr int kChargingCurrentMa = 30;
    static constexpr uint16_t kBatteryStatusDsg = BIT0;

    Charge(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
        read_buffer_ = new uint8_t[8];
    }
    ~Charge() { delete[] read_buffer_; }

    int16_t ReadWord(uint8_t reg) {
        uint8_t data[2] = {0};
        ReadRegs(reg, data, 2);
        return static_cast<int16_t>(static_cast<uint16_t>(data[0]) |
                                    (static_cast<uint16_t>(data[1]) << 8));
    }

    int GetBatteryLevel() {
        int level = ReadWord(kRegStateOfCharge);
        if (level < 0) {
            return 0;
        }
        if (level > 100) {
            return 100;
        }
        return level;
    }

    bool IsCharging() {
        const uint16_t status = static_cast<uint16_t>(ReadWord(kRegBatteryStatus));
        if ((status & kBatteryStatusDsg) == 0) {
            return true;
        }

        const int16_t avg_current_ma = ReadWord(kRegAverageCurrent);
        const int16_t current_ma = ReadWord(kRegCurrent);
        // Current is a fallback only: AverageCurrent can lag charger insertion noticeably.
        return avg_current_ma > kChargingCurrentMa || current_ma > kChargingCurrentMa;
    }

    bool IsDischarging() {
        return (static_cast<uint16_t>(ReadWord(kRegBatteryStatus)) & kBatteryStatusDsg) != 0;
    }

    void Update() {
        const int16_t voltage = ReadWord(kRegVoltage);
        const int16_t current = ReadWord(kRegCurrent);
        ESP_ERROR_CHECK(temperature_sensor_get_celsius(temp_sensor, &tsens_value));
        (void)voltage;
        (void)current;
    }

private:
    uint8_t* read_buffer_ = nullptr;
};

class Cst816s : public I2cDevice {
public:
    struct TouchPoint_t {
        int num = 0;
        int x = -1;
        int y = -1;
    };

    enum TouchEvent { TOUCH_NONE, TOUCH_PRESS, TOUCH_RELEASE, TOUCH_HOLD };

    Cst816s(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
        read_buffer_ = new uint8_t[6];
        was_touched_ = false;
        press_count_ = 0;

        // Create touch interrupt semaphore
        touch_isr_mux_ = xSemaphoreCreateBinary();
        if (touch_isr_mux_ == NULL) {
            ESP_LOGE(TAG, "Failed to create touch semaphore");
        }
    }

    ~Cst816s() {
        delete[] read_buffer_;

        // Delete semaphore if it exists
        if (touch_isr_mux_ != NULL) {
            vSemaphoreDelete(touch_isr_mux_);
            touch_isr_mux_ = NULL;
        }
    }

    void UpdateTouchPoint() {
        ReadRegs(0x02, read_buffer_, 6);
        tp_.num = read_buffer_[0] & 0x0F;
        tp_.x = ((read_buffer_[1] & 0x0F) << 8) | read_buffer_[2];
        tp_.y = ((read_buffer_[3] & 0x0F) << 8) | read_buffer_[4];
    }

    const TouchPoint_t& GetTouchPoint() { return tp_; }

    TouchEvent CheckTouchEvent() {
        bool is_touched = (tp_.num > 0);
        TouchEvent event = TOUCH_NONE;

        if (is_touched && !was_touched_) {
            // Press event (transition from not touched to touched)
            press_count_++;
            event = TOUCH_PRESS;
            ESP_LOGI(TAG, "TOUCH PRESS - count: %d, x: %d, y: %d", press_count_, tp_.x, tp_.y);
        } else if (!is_touched && was_touched_) {
            // Release event (transition from touched to not touched)
            event = TOUCH_RELEASE;
            ESP_LOGI(TAG, "TOUCH RELEASE - total presses: %d", press_count_);
        } else if (is_touched && was_touched_) {
            // Continuous touch (hold)
            event = TOUCH_HOLD;
            ESP_LOGD(TAG, "TOUCH HOLD - x: %d, y: %d", tp_.x, tp_.y);
        }

        // Update previous state
        was_touched_ = is_touched;
        return event;
    }

    int GetPressCount() const { return press_count_; }

    void ResetPressCount() { press_count_ = 0; }

    // Semaphore management methods
    SemaphoreHandle_t GetTouchSemaphore() { return touch_isr_mux_; }

    bool WaitForTouchEvent(TickType_t timeout = portMAX_DELAY) {
        if (touch_isr_mux_ != NULL) {
            return xSemaphoreTake(touch_isr_mux_, timeout) == pdTRUE;
        }
        return false;
    }

    void NotifyTouchEvent() {
        if (touch_isr_mux_ != NULL) {
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            xSemaphoreGiveFromISR(touch_isr_mux_, &xHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
    }

private:
    uint8_t* read_buffer_ = nullptr;
    TouchPoint_t tp_;

    // Touch state tracking
    bool was_touched_;
    int press_count_;

    // Touch interrupt semaphore
    SemaphoreHandle_t touch_isr_mux_;
};

class EspVocat : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    i2c_bus_handle_t shared_i2c_bus_handle_ = nullptr;
    Cst816s* cst816s_;
    Charge* charge_;
    Button boot_button_;
    Display* display_ = nullptr;
    PwmBacklight* backlight_ = nullptr;
    esp_timer_handle_t touchpad_timer_;
    esp_lcd_touch_handle_t tp;  // LCD touch handle
    EspVideo* camera_ = nullptr;
    TaskHandle_t charge_task_handle_ = nullptr;
    TaskHandle_t touch_task_handle_ = nullptr;
    TaskHandle_t imu_task_handle_ = nullptr;
#if ESP_VOCAT_ENABLE_CAP_TOUCH_SENSOR
    TaskHandle_t touch_slider_task_handle_ = nullptr;
#endif
    esp_timer_handle_t emotion_reset_timer_ = nullptr;
    bool bmi270_ready_ = false;
    bool was_charging_ = false;
    // ===== VoCat panel mode (màn hình page: đồng hồ/lịch/thời tiết từ NAS, vuốt đổi) =====
    static constexpr const char* PANEL_HOST = "http://192.168.1.4:8080";
    bool panel_active_ = false;
    int panel_n_ = 0;
    int panel_count_ = 1;
    uint32_t touch_t0_ = 0;
    int touch_sx_ = 0, touch_sy_ = 0, touch_lx_ = 0, touch_ly_ = 0;
    // Màn media native (tên cuộn + giờ tick trên máy) — thay panel-ảnh cho audiobook, khỏi refresh ảnh.
    gfx_obj_t* media_title_ = nullptr;
    gfx_obj_t* media_author_ = nullptr;    // 2b: tác giả/ca sĩ
    gfx_obj_t* media_time_ = nullptr;
    gfx_obj_t* media_bar_bg_ = nullptr;    // 2b: thanh progress (nền)
    gfx_obj_t* media_bar_fg_ = nullptr;    // 2b: thanh progress (phần đã phát)
    esp_timer_handle_t media_timer_ = nullptr;
    int media_pos0_ = 0;                // vị trí (giây) lúc bắt đầu hiện
    int media_dur_ = 0;                 // tổng thời lượng (giây)
    int64_t media_t0_us_ = 0;           // mốc thời gian bắt đầu hiện
    bool media_active_ = false;
    bool media_bar_on_ = true;          // có vẽ thanh progress không (sách=có, nhạc=không)
    uint8_t low_battery_alert_mask_ = 0;
    int low_battery_plays_left_ = 0;
    int64_t next_low_battery_play_ms_ = 0;
#if ESP_VOCAT_ENABLE_CAP_TOUCH_SENSOR
    touch_slider_handle_t touch_slider_handle_ = nullptr;
    touch_button_handle_t touch_button_handle_ = nullptr;
#endif

    static void emotion_reset_timer_callback(void* arg) {
        auto* self = static_cast<EspVocat*>(arg);
        if (self && self->display_ != nullptr) {
            self->display_->SetEmotion("neutral");
        }
    }

    void ShowTemporaryEmotion(const char* emotion, uint32_t duration_ms) {
        if (display_ == nullptr || emotion == nullptr) {
            return;
        }
        display_->SetEmotion(emotion);
        if (emotion_reset_timer_ != nullptr) {
            esp_timer_stop(emotion_reset_timer_);
            esp_timer_start_once(emotion_reset_timer_,
                                 static_cast<uint64_t>(duration_ms) * 1000ULL);
        }
    }

    void ShowHappyTouchFeedback() {
        static int64_t s_last_us = 0;
        constexpr int64_t kCooldownUs = 1200000;
        const int64_t now = esp_timer_get_time();
        if ((now - s_last_us) < kCooldownUs) {
            return;
        }
        s_last_us = now;
        ShowTemporaryEmotion("happy", 2000);
    }

    void PlayBatteryEmotion(const char* emotion, uint32_t duration_ms) {
#if CONFIG_USE_EMOTE_MESSAGE_STYLE
        if (display_ == nullptr || emotion == nullptr) {
            return;
        }
        auto* emote_display = dynamic_cast<emote::EmoteDisplay*>(display_);
        if (emote_display != nullptr) {
            emote_display->InsertAnimDialog(emotion, duration_ms);
            return;
        }
#endif
        ShowTemporaryEmotion(emotion, duration_ms);
    }

    void StartLowBatteryAlertSequence() {
        constexpr int kMaxPlays = 3;
        constexpr int64_t kIntervalMs = 5 * 60 * 1000;

        low_battery_plays_left_ = kMaxPlays - 1;
        next_low_battery_play_ms_ = (esp_timer_get_time() / 1000) + kIntervalMs;
        PlayBatteryEmotion("low_battery", 4000);
    }

    void HandleBatteryEmotions() {
        if (charge_ == nullptr) {
            return;
        }

        const int level = charge_->GetBatteryLevel();
        const bool charging = charge_->IsCharging();

        if (charging && !was_charging_) {
            PlayBatteryEmotion("battery_connected", 4000);
            low_battery_alert_mask_ = 0;
            low_battery_plays_left_ = 0;
            next_low_battery_play_ms_ = 0;
        }
        was_charging_ = charging;

        if (charging) {
            return;
        }

        if (level > 12) {
            low_battery_alert_mask_ = 0;
            low_battery_plays_left_ = 0;
            next_low_battery_play_ms_ = 0;
            return;
        }

        const int64_t now_ms = esp_timer_get_time() / 1000;
        if (low_battery_plays_left_ > 0 && now_ms >= next_low_battery_play_ms_) {
            PlayBatteryEmotion("low_battery", 4000);
            low_battery_plays_left_--;
            if (low_battery_plays_left_ > 0) {
                next_low_battery_play_ms_ = now_ms + 5 * 60 * 1000;
            }
        }

        if (level <= 5 && (low_battery_alert_mask_ & 0x02) == 0) {
            low_battery_alert_mask_ |= 0x02;
            StartLowBatteryAlertSequence();
            return;
        }

        if (level <= 10 && (low_battery_alert_mask_ & 0x01) == 0) {
            low_battery_alert_mask_ |= 0x01;
            StartLowBatteryAlertSequence();
        }
    }

    static void battery_task(void* arg) {
        auto* self = static_cast<EspVocat*>(arg);
        while (true) {
            if (self != nullptr && self->charge_ != nullptr) {
                self->charge_->Update();
                self->HandleBatteryEmotions();
            }
            vTaskDelay(pdMS_TO_TICKS(300));
        }
    }

    static void imu_event_task(void* arg) {
        auto* self = static_cast<EspVocat*>(arg);
        if (self == nullptr || !self->bmi270_ready_) {
            vTaskDelete(NULL);
            return;
        }

        struct bmi2_sens_data prev = {};
        struct bmi2_sens_data cur = {};
        bool has_prev = false;
        int64_t last_shake_ms = 0;
        constexpr int kShakeDeltaThreshold = 20000;
        constexpr int64_t kShakeCooldownMs = 2000;

        while (true) {
            if (Bmi270Motion::ReadAccelRaw(cur)) {
                if (has_prev) {
                    int dx = abs(static_cast<int>(cur.acc.x) - static_cast<int>(prev.acc.x));
                    int dy = abs(static_cast<int>(cur.acc.y) - static_cast<int>(prev.acc.y));
                    int dz = abs(static_cast<int>(cur.acc.z) - static_cast<int>(prev.acc.z));
                    int shake_score = dx + dy + dz;

                    int64_t now_ms = esp_timer_get_time() / 1000;
                    if (shake_score > kShakeDeltaThreshold &&
                        (now_ms - last_shake_ms) > kShakeCooldownMs) {
                        last_shake_ms = now_ms;
                        // TEST xem hết bộ: mỗi lần lắc = emote KẾ TIẾP (23 cái vector).
                        static const char* kTestEmotes[] = {
                            "angry", "asleep", "badminton", "confident", "cry", "investigate",
                            "laugh", "leisure", "mock", "music", "mute", "panic", "ponder",
                            "question", "sad", "shocked", "shy", "sigh", "smile", "smile_static",
                            "snigger", "yawn", "yummy"};
                        static int kTestIdx = 0;
                        const char* em = kTestEmotes[kTestIdx % 23];
                        kTestIdx++;
                        ESP_LOGI("VOCAT_EMOTE", "shake #%d -> %s", kTestIdx, em);
                        self->ShowTemporaryEmotion(em, 4500);
                    }
                }
                prev = cur;
                has_prev = true;
            }
            vTaskDelay(pdMS_TO_TICKS(80));
        }
    }

    void InitializeI2c() {
        i2c_config_t i2c_cfg = {
            .mode = I2C_MODE_MASTER,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .sda_pullup_en = true,
            .scl_pullup_en = true,
            .master =
                {
                    .clk_speed = 400000,
                },
            .clk_flags = 0,
        };
        shared_i2c_bus_handle_ = i2c_bus_create(I2C_NUM_0, &i2c_cfg);
        if (!shared_i2c_bus_handle_) {
            ESP_LOGE(TAG, "Failed to create shared I2C bus");
            ESP_ERROR_CHECK(ESP_FAIL);
        }
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0) && !CONFIG_I2C_BUS_BACKWARD_CONFIG
        i2c_bus_ = i2c_bus_get_internal_bus_handle(shared_i2c_bus_handle_);
#else
#error "ESP-VoCat board requires i2c_bus_get_internal_bus_handle() support"
#endif
        if (!i2c_bus_) {
            ESP_LOGE(TAG, "Failed to get I2C master handle");
            ESP_ERROR_CHECK(ESP_FAIL);
        }

        temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
        ESP_ERROR_CHECK(temperature_sensor_install(&temp_sensor_config, &temp_sensor));
        ESP_ERROR_CHECK(temperature_sensor_enable(temp_sensor));
    }
    uint8_t DetectPcbVersion() {
        gpio_config_t gpio_conf = {.pin_bit_mask = (1ULL << CORDEC_POWER_CTRL),
                                   .mode = GPIO_MODE_OUTPUT,
                                   .pull_up_en = GPIO_PULLUP_DISABLE,
                                   .pull_down_en = GPIO_PULLDOWN_DISABLE,
                                   .intr_type = GPIO_INTR_DISABLE};
        ESP_ERROR_CHECK(gpio_config(&gpio_conf));
        ESP_ERROR_CHECK(gpio_set_level(CORDEC_POWER_CTRL, 0));
        vTaskDelay(pdMS_TO_TICKS(50));

        bool codec_alive = (i2c_master_probe(i2c_bus_, 0x18, 100) == ESP_OK);
        uint8_t pcb_version = 0;
        if (codec_alive) {
            ESP_LOGI(TAG, "PCB version V1.0");
            pcb_version = 0;
        } else {
            ESP_ERROR_CHECK(gpio_set_level(CORDEC_POWER_CTRL, 1));
            vTaskDelay(pdMS_TO_TICKS(50));
            codec_alive = (i2c_master_probe(i2c_bus_, 0x18, 100) == ESP_OK);
            if (codec_alive) {
                ESP_LOGI(TAG, "PCB version V1.2");
                pcb_version = 1;
                AUDIO_I2S_GPIO_DIN = AUDIO_I2S_GPIO_DIN_2;
                AUDIO_CODEC_PA_PIN = AUDIO_CODEC_PA_PIN_2;
                QSPI_PIN_NUM_LCD_RST = QSPI_PIN_NUM_LCD_RST_2;
                TOUCH_PAD2 = TOUCH_PAD2_2;
                UART1_TX = UART1_TX_2;
                UART1_RX = UART1_RX_2;
            } else {
                ESP_LOGE(TAG, "PCB version detection error");
            }
        }
        return pcb_version;
    }

    static void touch_isr_callback(void* arg) {
        Cst816s* touchpad = static_cast<Cst816s*>(arg);
        if (touchpad != nullptr) {
            touchpad->NotifyTouchEvent();
        }
    }

    // Tải ảnh JPEG từ 1 URL -> hiện lên màn (đè mặt mèo). Trả true nếu hiện được.
    bool FetchUrlAndShow(const char* url) {
        auto* disp = dynamic_cast<emote::EmoteDisplay*>(display_);
        if (disp == nullptr) return false;
        esp_http_client_config_t cfg = {};
        cfg.url = url;
        cfg.timeout_ms = 8000;
        esp_http_client_handle_t c = esp_http_client_init(&cfg);
        if (c == nullptr) return false;
        bool ok = false;
        uint8_t* buf = nullptr;
        do {
            if (esp_http_client_open(c, 0) != ESP_OK) break;
            int clen = esp_http_client_fetch_headers(c);
            if (clen <= 0 || clen > 200000) break;
            char* cnt = nullptr;
            if (esp_http_client_get_header(c, "X-Panel-Count", &cnt) == ESP_OK && cnt) {
                int v = atoi(cnt);
                if (v > 0) panel_count_ = v;
            }
            buf = (uint8_t*)malloc(clen);
            if (buf == nullptr) break;
            int off = 0, r = 0;
            while (off < clen && (r = esp_http_client_read(c, (char*)buf + off, clen - off)) > 0) {
                off += r;
            }
            if (off == clen && disp->ShowPanelImage(buf, clen)) {
                panel_active_ = true;
                ok = true;
            }
        } while (0);
        if (buf) free(buf);
        esp_http_client_close(c);
        esp_http_client_cleanup(c);
        return ok;
    }

    // Tải ảnh page thứ n từ NAS (/panel?mac=<mac mèo>&n=) -> hiện lên màn (đường VUỐT).
    void FetchAndShowPanel(int n) {
        uint8_t mac[6] = {0};
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        char url[160];
        snprintf(url, sizeof(url), "%s/panel?mac=%02x:%02x:%02x:%02x:%02x:%02x&n=%d",
                 PANEL_HOST, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], n);
        FetchUrlAndShow(url);
    }

    // Fetch+decode blocking vài giây -> KHÔNG chạy trong callback MCP (task chính); đẩy sang task riêng.
    struct PanelFetchReq { EspVocat* self; char url[200]; };
    static void PanelFetchTask(void* arg) {
        auto* rq = static_cast<PanelFetchReq*>(arg);
        rq->self->FetchUrlAndShow(rq->url);
        free(rq);
        vTaskDelete(nullptr);
    }

    // Tải bìa JPEG (đã resize sẵn ở HomeCenter) -> hiện lên đỉnh màn media (KHÔNG đè mặt mèo — media tự ẩn mặt).
    bool FetchUrlAndShowCover(const char* url) {
        auto* disp = dynamic_cast<emote::EmoteDisplay*>(display_);
        if (disp == nullptr) return false;
        esp_http_client_config_t cfg = {};
        cfg.url = url;
        cfg.timeout_ms = 8000;
        esp_http_client_handle_t c = esp_http_client_init(&cfg);
        if (c == nullptr) return false;
        bool ok = false;
        uint8_t* buf = nullptr;
        do {
            if (esp_http_client_open(c, 0) != ESP_OK) break;
            int clen = esp_http_client_fetch_headers(c);
            if (clen <= 0 || clen > 200000) break;
            buf = (uint8_t*)malloc(clen);
            if (buf == nullptr) break;
            int off = 0, r = 0;
            while (off < clen && (r = esp_http_client_read(c, (char*)buf + off, clen - off)) > 0) {
                off += r;
            }
            if (off == clen && media_active_ && disp->ShowMediaCover(buf, clen)) ok = true;
        } while (0);
        if (buf) free(buf);
        esp_http_client_close(c);
        esp_http_client_cleanup(c);
        return ok;
    }
    static void MediaCoverFetchTask(void* arg) {
        auto* rq = static_cast<PanelFetchReq*>(arg);
        rq->self->FetchUrlAndShowCover(rq->url);
        free(rq);
        vTaskDelete(nullptr);
    }

    // ---- Màn MEDIA native: tên (cuộn mượt) + giờ (tick trên máy). Bỏ refresh ảnh -> không tải server, không giựt audio. ----
    static void FmtTime(char* buf, size_t n, int s) {
        if (s < 0) s = 0;
        if (s < 3600) snprintf(buf, n, "%d:%02d", s / 60, s % 60);
        else snprintf(buf, n, "%d:%02d:%02d", s / 3600, (s % 3600) / 60, s % 60);
    }

    void UpdateMediaTimeLabel() {
        auto* disp = dynamic_cast<emote::EmoteDisplay*>(display_);
        if (disp == nullptr || !media_active_) return;
        int pos = media_pos0_ + (int)((esp_timer_get_time() - media_t0_us_) / 1000000);
        if (media_dur_ > 0 && pos > media_dur_) pos = media_dur_;
        char a[16], b[16], line[40];
        FmtTime(a, sizeof(a), pos);
        FmtTime(b, sizeof(b), media_dur_);
        snprintf(line, sizeof(line), "%s / %s", a, b);       // "đã nghe / tổng" = tiến độ dạng số
        // Thanh progress MỊN: mỗi ô 2 mức nhờ khối NỬA ▌ -> 2*CELLS mức (mịn gấp đôi 10 ô đặc).
        // UTF-8: █=E2 96 88 (đầy), ▌=E2 96 8C (nửa trái), ░=E2 96 91 (rỗng).
        const int CELLS = 12;
        int halves = (media_dur_ > 0) ? (int)(((int64_t)2 * CELLS * pos + media_dur_ / 2) / media_dur_) : 0;
        if (halves < 0) halves = 0;
        if (halves > 2 * CELLS) halves = 2 * CELLS;
        int full = halves / 2, half = halves % 2;
        char bar[CELLS * 3 + 4]; int bp = 0;
        for (int i = 0; i < full; i++) { bar[bp++] = '\xE2'; bar[bp++] = '\x96'; bar[bp++] = '\x88'; }        // █
        if (half && full < CELLS) { bar[bp++] = '\xE2'; bar[bp++] = '\x96'; bar[bp++] = '\x8C'; }             // ▌
        for (int i = full + half; i < CELLS; i++) { bar[bp++] = '\xE2'; bar[bp++] = '\x96'; bar[bp++] = '\x91'; }  // ░
        bar[bp] = 0;
        emote_handle_t h = disp->GetEmoteHandle();
        emote_lock(h);
        if (media_time_ != nullptr) gfx_label_set_text(media_time_, line);
        if (media_bar_on_ && media_bar_fg_ != nullptr) gfx_label_set_text(media_bar_fg_, bar);
        emote_unlock(h);
        emote_notify_all_refresh(h);
    }

    static void MediaTickCb(void* arg) { static_cast<EspVocat*>(arg)->UpdateMediaTimeLabel(); }

    void ShowMedia(const char* title, const char* author, int pos, int dur, const char* cover_url, bool show_bar) {
        auto* disp = dynamic_cast<emote::EmoteDisplay*>(display_);
        if (disp == nullptr) return;
        emote_handle_t h = disp->GetEmoteHandle();
        if (h == nullptr) return;
        bool has_cover = (cover_url != nullptr && cover_url[0] != '\0');
        (void)author; (void)dur;   // tác giả + dur chưa dùng trực tiếp ở đây (dur qua media_dur_)
        // Bìa dim+gradient phủ FULL màn -> chữ đặt ở KHU ĐÁY (vùng tối). Không bìa -> chữ căn giữa trên nền đen.
        const int title_ofs = has_cover ? 58 : -34;    // GFX_ALIGN_CENTER y offset (center=180)
        const int time_ofs  = has_cover ? 96 : 6;
        const int prog_ofs  = has_cover ? 132 : 44;    // thanh progress (label ký tự ▓░) dưới dòng giờ
        // Tạo obj ảnh nền TRƯỚC MỌI label (kể cả khi chưa có bìa) -> nền luôn nằm DƯỚI chữ (z-order theo thứ tự tạo).
        disp->CreateMediaCoverObj();
        if (!has_cover) disp->HideMediaCover();                     // không bìa -> ẩn nền cũ (nếu còn); tự khoá, NGOÀI lock
        emote_lock(h);
        // Tên sách/bài (cuộn native) — chữ trắng nổi trên nền tối
        if (media_title_ == nullptr) media_title_ = emote_create_obj_by_type(h, "label", "media_title");
        if (media_title_ != nullptr) {
            gfx_label_set_font(media_title_, (void*)&vocat_vn_26);
            gfx_label_set_color(media_title_, GFX_COLOR_HEX(0xFFFFFF));
            gfx_obj_set_size(media_title_, 300, 40);
            gfx_label_set_scroll_speed(media_title_, 50);
            gfx_label_set_text(media_title_, title ? title : "");
            gfx_label_set_long_mode(media_title_, GFX_LABEL_LONG_SCROLL);
            gfx_obj_align(media_title_, GFX_ALIGN_CENTER, 0, title_ofs);
            gfx_obj_set_visible(media_title_, true);
        }
        // Giờ "đã nghe / tổng". PHẢI set_size + set_text (giống title) mới render — label không size = không hiện.
        if (media_time_ == nullptr) media_time_ = emote_create_obj_by_type(h, "label", "media_time");
        if (media_time_ != nullptr) {
            gfx_label_set_font(media_time_, (void*)&vocat_vn_26);
            gfx_label_set_color(media_time_, GFX_COLOR_HEX(0xEAF6F3));
            gfx_obj_set_size(media_time_, 300, 40);
            gfx_label_set_text(media_time_, "0:00 / 0:00");
            gfx_label_set_text_align(media_time_, GFX_TEXT_ALIGN_CENTER);
            gfx_obj_align(media_time_, GFX_ALIGN_CENTER, 0, time_ofs);
            gfx_obj_set_visible(media_time_, true);
        }
        // Thanh progress = 1 label PLAIN vẽ bằng ký tự khối (█ đã phát / ░ còn lại). Nhạc KHÔNG cần -> show_bar=false thì ẩn.
        media_bar_on_ = show_bar;
        if (show_bar) {
            if (media_bar_fg_ == nullptr) media_bar_fg_ = emote_create_obj_by_type(h, "label", "media_prog");
            if (media_bar_fg_ != nullptr) {
                gfx_label_set_font(media_bar_fg_, (void*)&vocat_vn_26);
                gfx_label_set_color(media_bar_fg_, GFX_COLOR_HEX(0x7FD1C4));
                gfx_obj_set_size(media_bar_fg_, 300, 40);
                gfx_label_set_text(media_bar_fg_, "\xE2\x96\x91\xE2\x96\x91\xE2\x96\x91\xE2\x96\x91\xE2\x96\x91\xE2\x96\x91\xE2\x96\x91\xE2\x96\x91\xE2\x96\x91\xE2\x96\x91\xE2\x96\x91\xE2\x96\x91");
                gfx_label_set_text_align(media_bar_fg_, GFX_TEXT_ALIGN_CENTER);
                gfx_obj_align(media_bar_fg_, GFX_ALIGN_CENTER, 0, prog_ofs);
                gfx_obj_set_visible(media_bar_fg_, true);
            }
        } else if (media_bar_fg_ != nullptr) {
            gfx_obj_set_visible(media_bar_fg_, false);   // nhạc: ẩn thanh progress (nếu còn từ lần phát sách)
        }
        emote_set_anim_visible(h, false);                          // ẩn mặt mèo
        emote_unlock(h);
        emote_notify_all_refresh(h);
        ESP_LOGI(TAG, "media objs: title=%d time=%d cover=%d",
                 media_title_ != nullptr, media_time_ != nullptr, has_cover);
        media_pos0_ = pos; media_dur_ = dur; media_t0_us_ = esp_timer_get_time(); media_active_ = true;
        // Ảnh nền: fetch+decode chậm -> đẩy sang task riêng (đặt media_active_ trước để task biết còn cần hiện).
        if (has_cover) {
            auto* rq = static_cast<PanelFetchReq*>(malloc(sizeof(PanelFetchReq)));
            if (rq != nullptr) {
                rq->self = this;
                snprintf(rq->url, sizeof(rq->url), "%s", cover_url);
                xTaskCreatePinnedToCore(MediaCoverFetchTask, "media_cover", 4 * 1024, rq, 5, nullptr, 0);
            }
        }
        UpdateMediaTimeLabel();
        if (media_timer_ == nullptr) {
            esp_timer_create_args_t a = {};
            a.callback = MediaTickCb; a.arg = this; a.name = "media_tick";
            esp_timer_create(&a, &media_timer_);
        }
        esp_timer_stop(media_timer_);
        esp_timer_start_periodic(media_timer_, 1000000);           // tick mỗi 1s (giờ nhảy trên máy)
    }

    void HideMedia() {
        if (!media_active_) return;
        media_active_ = false;
        if (media_timer_ != nullptr) esp_timer_stop(media_timer_);
        auto* disp = dynamic_cast<emote::EmoteDisplay*>(display_);
        if (disp == nullptr) return;
        emote_handle_t h = disp->GetEmoteHandle();
        emote_lock(h);
        if (media_title_ != nullptr) gfx_obj_set_visible(media_title_, false);
        if (media_time_ != nullptr) gfx_obj_set_visible(media_time_, false);
        if (media_bar_fg_ != nullptr) gfx_obj_set_visible(media_bar_fg_, false);   // thanh progress
        emote_set_anim_visible(h, true);                           // trả mặt mèo
        emote_unlock(h);
        disp->HideMediaCover();                                    // ẩn ảnh nền media (tự khoá, gọi NGOÀI lock)
        emote_notify_all_refresh(h);
    }

    // Đóng panel (lịch/thời tiết...). Nếu ĐANG phát media -> quay về màn media (giữ ẩn mặt mèo),
    // KHÔNG bật lại mặt mèo (nếu bật thì mặt mèo chồng lên chữ media -> bug "kẹt hai mắt mèo").
    void DismissPanel() {
        auto* disp = dynamic_cast<emote::EmoteDisplay*>(display_);
        if (disp == nullptr) return;
        disp->HidePanel();                                         // ẩn ảnh panel + (mặc định) bật lại mặt mèo
        panel_active_ = false;
        if (media_active_) {                                       // còn phát -> media labels vẫn hiện, chỉ cần ẩn lại mặt mèo
            emote_handle_t h = disp->GetEmoteHandle();
            emote_lock(h);
            emote_set_anim_visible(h, false);
            emote_unlock(h);
            emote_notify_all_refresh(h);
        }
    }

    // Đăng ký tool MCP: brain (khi user "Mèo cho xem lịch") gọi -> hiện panel của người chỉ định.
    void InitializeTools() {
        auto& mcp = McpServer::GetInstance();
        mcp.AddTool(
            "self.screen.show_panel",
            "Hiển thị trang thông tin của một người lên màn hình. Tham số person = tên người "
            "(vd Ja, Men), view = loại trang (calendar/weather/clock).",
            PropertyList({
                Property("person", kPropertyTypeString),
                Property("view", kPropertyTypeString, std::string("calendar")),
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                std::string person = properties["person"].value<std::string>();
                std::string view = properties["view"].value<std::string>();
                auto* rq = static_cast<PanelFetchReq*>(malloc(sizeof(PanelFetchReq)));
                if (rq == nullptr) return false;
                rq->self = this;
                snprintf(rq->url, sizeof(rq->url), "%s/panel?person=%s&view=%s",
                         PANEL_HOST, person.c_str(), view.c_str());
                xTaskCreatePinnedToCore(PanelFetchTask, "panel_fetch", 4 * 1024, rq, 5, nullptr, 0);
                return true;
            });
        // Đóng panel -> trả màn về mặt mèo. Brain gọi khi DỪNG phát nhạc/sách (server show_panel không tự tắt).
        mcp.AddTool(
            "self.screen.hide_panel",
            "Đóng panel đang hiện trên màn hình, trả về mặt mèo. Gọi khi dừng phát nhạc/sách "
            "hoặc khi không cần hiển thị panel nữa.",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                auto* disp = dynamic_cast<emote::EmoteDisplay*>(display_);
                if (disp != nullptr) disp->HidePanel();
                HideMedia();                                   // đóng cả màn media native (nếu đang hiện)
                panel_active_ = false;
                return true;
            });
        // Màn "đang phát" NATIVE (tên cuộn + giờ tick trên máy) — thay panel-ảnh cho audiobook/nhạc, khỏi refresh ảnh.
        mcp.AddTool(
            "self.screen.media_show",
            "Hiển thị màn đang phát NATIVE: tên bài/sách (chữ tự cuộn ngang) + thời gian (tự nhảy trên máy). "
            "Gọi 1 lần khi bắt đầu phát nhạc/sách nói. title=tên, pos=số giây đã phát, dur=tổng số giây, "
            "cover=URL ảnh bìa (đã resize sẵn, để trống nếu không có), bar=1 hiện thanh tiến độ (sách) / 0 ẩn (nhạc).",
            PropertyList({
                Property("title", kPropertyTypeString, std::string("")),
                Property("author", kPropertyTypeString, std::string("")),
                Property("pos", kPropertyTypeInteger, 0),
                Property("dur", kPropertyTypeInteger, 0),
                Property("cover", kPropertyTypeString, std::string("")),
                Property("bar", kPropertyTypeInteger, 1),
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                std::string title = properties["title"].value<std::string>();
                std::string author = properties["author"].value<std::string>();
                int pos = properties["pos"].value<int>();
                int dur = properties["dur"].value<int>();
                std::string cover = properties["cover"].value<std::string>();
                int bar = properties["bar"].value<int>();
                ShowMedia(title.c_str(), author.c_str(), pos, dur, cover.c_str(), bar != 0);
                return true;
            });
    }

    void OnTouchStart(int x, int y) {
        touch_t0_ = (uint32_t)(esp_timer_get_time() / 1000);
        touch_sx_ = touch_lx_ = x;
        touch_sy_ = touch_ly_ = y;
    }

    void OnTouchMove(int x, int y) { touch_lx_ = x; touch_ly_ = y; }

    void OnTouchEnd() {
        int dx = touch_lx_ - touch_sx_, dy = touch_ly_ - touch_sy_;
        const int SW = 45;                                  // ngưỡng vuốt (px)
        auto& app = Application::GetInstance();
        if (abs(dx) >= SW && abs(dx) >= abs(dy)) {          // vuốt NGANG -> vào/đổi page
            if (!panel_active_) panel_n_ = 0;
            else panel_n_ += (dx < 0 ? 1 : -1);             // vuốt trái = trang kế, phải = trang trước
            FetchAndShowPanel(panel_n_);
        } else if (dy <= -SW && abs(dy) > abs(dx)) {        // vuốt LÊN -> thoát panel
            if (panel_active_) DismissPanel();
        } else {                                            // CHẠM (di chuyển nhỏ)
            if (panel_active_) {                            // đang xem panel -> chạm để thoát
                DismissPanel();
            } else if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
            } else {
                app.ToggleChatState();                      // chạm thường = nói chuyện (như cũ)
            }
        }
    }

    static void touch_event_task(void* arg) {
        Cst816s* touchpad = static_cast<Cst816s*>(arg);
        if (touchpad == nullptr) {
            ESP_LOGE(TAG, "Invalid touchpad pointer in touch_event_task");
            vTaskDelete(NULL);
            return;
        }

        while (true) {
            if (touchpad->WaitForTouchEvent()) {
                auto& board = (EspVocat&)Board::GetInstance();

                ESP_LOGD(TAG, "Touch event, TP_PIN_NUM_INT: %d", gpio_get_level(TP_PIN_NUM_INT));
                touchpad->UpdateTouchPoint();
                auto touch_event = touchpad->CheckTouchEvent();
                const auto& tpt = touchpad->GetTouchPoint();

                if (touch_event == Cst816s::TOUCH_PRESS) {
                    board.OnTouchStart(tpt.x, tpt.y);
                } else if (touch_event == Cst816s::TOUCH_HOLD) {
                    board.OnTouchMove(tpt.x, tpt.y);
                } else if (touch_event == Cst816s::TOUCH_RELEASE) {
                    board.OnTouchEnd();       // quyết định: vuốt page / thoát / chạm nói chuyện
                }
            }
        }
    }

    void InitializeCharge() {
        charge_ = new Charge(i2c_bus_, 0x55);
        was_charging_ = charge_->IsCharging();
        xTaskCreatePinnedToCore(battery_task, "batteryTask", 3 * 1024, this, 6,
                                &charge_task_handle_, 0);
    }

    void InitializeCst816sTouchPad() {
        cst816s_ = new Cst816s(i2c_bus_, 0x15);

        xTaskCreatePinnedToCore(touch_event_task, "touch_task", 4 * 1024, cst816s_, 5,
                                &touch_task_handle_, 1);

        const gpio_config_t int_gpio_config = {.pin_bit_mask = (1ULL << TP_PIN_NUM_INT),
                                               .mode = GPIO_MODE_INPUT,
                                               // .intr_type = GPIO_INTR_NEGEDGE
                                               .intr_type = GPIO_INTR_ANYEDGE};
        gpio_config(&int_gpio_config);
        gpio_install_isr_service(0);
        gpio_intr_enable(TP_PIN_NUM_INT);
        gpio_isr_handler_add(TP_PIN_NUM_INT, EspVocat::touch_isr_callback, cst816s_);
    }

    void InitializeBmi270() {
        esp_err_t imu_ret = Bmi270Motion::Initialize(shared_i2c_bus_handle_);
        if (imu_ret == ESP_OK) {
            bmi270_ready_ = true;
            xTaskCreatePinnedToCore(imu_event_task, "imu_task", 4 * 1024, this, 4,
                                    &imu_task_handle_, 1);
        } else {
            ESP_LOGW(TAG, "BMI270 unavailable, shake emotion disabled");
        }
    }

#if ESP_VOCAT_ENABLE_CAP_TOUCH_SENSOR
    static uint32_t TouchChannelFromPadGpio(gpio_num_t gpio) {
        if (gpio == GPIO_NUM_NC) {
            return 0;
        }
        if (gpio >= GPIO_NUM_1 && gpio <= GPIO_NUM_14) {
            return static_cast<uint32_t>(gpio);
        }
        return 0;
    }

    static void touch_slider_event_callback(touch_slider_handle_t handle,
                                            touch_slider_event_t event, int32_t data,
                                            void* cb_arg) {
        (void)handle;
        auto* self = static_cast<EspVocat*>(cb_arg);
        if (self == nullptr || self->display_ == nullptr) {
            return;
        }
        if (event != TOUCH_SLIDER_EVENT_POSITION) {
            ESP_LOGI(TAG, "Touch slider evt=%d data=%" PRId32, static_cast<int>(event), data);
        }

        bool gesture = false;
        if (event == TOUCH_SLIDER_EVENT_LEFT_SWIPE || event == TOUCH_SLIDER_EVENT_RIGHT_SWIPE) {
            gesture = true;
        } else if (event == TOUCH_SLIDER_EVENT_RELEASE) {
            gesture = true;
        }

        if (!gesture) {
            return;
        }

        self->ShowHappyTouchFeedback();
    }

    static void touch_button_event_callback(touch_button_handle_t handle, uint32_t channel,
                                            touch_state_t state, void* cb_arg) {
        (void)handle;
        auto* self = static_cast<EspVocat*>(cb_arg);
        if (self == nullptr || self->display_ == nullptr) {
            return;
        }
        if (state == TOUCH_STATE_ACTIVE) {
            ESP_LOGI(TAG, "Touch button ACTIVE ch=%" PRIu32, channel);
            self->ShowHappyTouchFeedback();
        }
    }

    static void touch_cap_poll_task(void* arg) {
        auto* self = static_cast<EspVocat*>(arg);
        while (true) {
            if (self != nullptr) {
                if (self->touch_slider_handle_ != nullptr) {
                    touch_slider_sensor_handle_events(self->touch_slider_handle_);
                } else if (self->touch_button_handle_ != nullptr) {
                    touch_button_sensor_handle_events(self->touch_button_handle_);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    void InitializeCapacitiveTouchPads() {
        if (TOUCH_PAD1 == GPIO_NUM_NC) {
            ESP_LOGW(TAG, "Capacitive touch disabled: TOUCH_PAD1 NC");
            return;
        }

        const uint32_t ch1 = TouchChannelFromPadGpio(TOUCH_PAD1);
        if (ch1 == 0) {
            ESP_LOGW(TAG, "TOUCH_PAD1 GPIO %d is not a touch channel (expect GPIO1..GPIO14)",
                     (int)TOUCH_PAD1);
            return;
        }

        if (TOUCH_PAD2 != GPIO_NUM_NC) {
            const uint32_t ch2 = TouchChannelFromPadGpio(TOUCH_PAD2);
            if (ch2 == 0) {
                ESP_LOGW(TAG, "TOUCH_PAD2 GPIO %d is not a touch channel", (int)TOUCH_PAD2);
                return;
            }

            static uint32_t slider_ch[2];
            static float slider_thr[2];
            slider_ch[0] = ch1;
            slider_ch[1] = ch2;
            slider_thr[0] = 0.004f;
            slider_thr[1] = 0.006f;

            touch_slider_config_t sld_cfg = {
                .channel_num = 2,
                .channel_list = slider_ch,
                .channel_threshold = slider_thr,
                .channel_gold_value = nullptr,
                .debounce_times = 1,
                .filter_reset_times = 5,
                .position_range = 10000,
                .calculate_window = 2,
                .swipe_threshold = 28.f,
                .swipe_hysterisis = 22.f,
                .swipe_alpha = 0.9f,
                .skip_lowlevel_init = false,
            };
            esp_err_t err = touch_slider_sensor_create(&sld_cfg, &touch_slider_handle_,
                                                       touch_slider_event_callback, this);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "touch_slider_sensor_create failed: %s", esp_err_to_name(err));
                touch_slider_handle_ = nullptr;
                return;
            }
            xTaskCreatePinnedToCore(touch_cap_poll_task, "touch_cap", 3072, this, 3,
                                    &touch_slider_task_handle_, 1);
            ESP_LOGI(TAG, "Touch slider (PCB v1.2+): PAD1 GPIO%d ch%u, PAD2 GPIO%d ch%u",
                     (int)TOUCH_PAD1, (unsigned)slider_ch[0], (int)TOUCH_PAD2,
                     (unsigned)slider_ch[1]);
            return;
        }

        static uint32_t btn_ch[1];
        static float btn_thr[1];
        btn_ch[0] = ch1;
        btn_thr[0] = 0.004f;

        touch_button_config_t btn_cfg = {
            .channel_num = 1,
            .channel_list = btn_ch,
            .channel_threshold = btn_thr,
            .channel_gold_value = nullptr,
            .debounce_times = 2,
            .skip_lowlevel_init = false,
        };
        esp_err_t err = touch_button_sensor_create(&btn_cfg, &touch_button_handle_,
                                                   touch_button_event_callback, this);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "touch_button_sensor_create failed: %s", esp_err_to_name(err));
            touch_button_handle_ = nullptr;
            return;
        }
        xTaskCreatePinnedToCore(touch_cap_poll_task, "touch_cap", 3072, this, 3,
                                &touch_slider_task_handle_, 1);
        ESP_LOGI(TAG, "Touch button (PCB v1.0): TOUCH_PAD1 GPIO%d ch%u", (int)TOUCH_PAD1,
                 (unsigned)btn_ch[0]);
    }
#endif

    void InitializeSpi() {
        const spi_bus_config_t bus_config = TAIJIPI_ST77916_PANEL_BUS_QSPI_CONFIG(
            QSPI_PIN_NUM_LCD_PCLK, QSPI_PIN_NUM_LCD_DATA0, QSPI_PIN_NUM_LCD_DATA1,
            QSPI_PIN_NUM_LCD_DATA2, QSPI_PIN_NUM_LCD_DATA3, QSPI_LCD_H_RES * 80 * sizeof(uint16_t));
        ESP_ERROR_CHECK(spi_bus_initialize(QSPI_LCD_HOST, &bus_config, SPI_DMA_CH_AUTO));
    }

    void InitializeSt77916Display(uint8_t pcb_version) {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = QSPI_PIN_NUM_LCD_CS;
        io_config.dc_gpio_num = GPIO_NUM_NC;
        io_config.spi_mode = 0;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 32;
        io_config.lcd_param_bits = 8;
        io_config.flags.quad_mode = true;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)QSPI_LCD_HOST,
                                                 &io_config, &panel_io));
        st77916_vendor_config_t vendor_config = {
            .init_cmds = vendor_specific_init_yysj,
            .init_cmds_size = sizeof(vendor_specific_init_yysj) / sizeof(st77916_lcd_init_cmd_t),
            .flags =
                {
                    .use_qspi_interface = 1,
                },
        };
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = QSPI_PIN_NUM_LCD_RST;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = QSPI_LCD_BIT_PER_PIXEL;
        panel_config.flags.reset_active_high = pcb_version;
        panel_config.vendor_config = &vendor_config;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st77916(panel_io, &panel_config, &panel));

        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_disp_on_off(panel, true);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);

#if CONFIG_USE_EMOTE_MESSAGE_STYLE
        display_ = new emote::EmoteDisplay(panel, panel_io, DISPLAY_WIDTH, DISPLAY_HEIGHT);
#else
        display_ = new SpiLcdDisplay(panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                     DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X,
                                     DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
#endif
        backlight_ = new PwmBacklight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        backlight_->RestoreBrightness();
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                ESP_LOGI(TAG, "Boot button pressed, enter WiFi configuration mode");
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
        gpio_config_t power_gpio_config = {
            .pin_bit_mask = (BIT64(POWER_CTRL)),
            .mode = GPIO_MODE_OUTPUT,

        };
        ESP_ERROR_CHECK(gpio_config(&power_gpio_config));

        gpio_set_level(POWER_CTRL, 0);
    }

#ifdef CONFIG_ESP_VIDEO_ENABLE_USB_UVC_VIDEO_DEVICE
    void InitializeCamera() {
        esp_video_init_usb_uvc_config_t usb_uvc_config = {
            .uvc =
                {
                    .uvc_dev_num = 1,
                    .task_stack = 4096,
                    .task_priority = 5,
                    .task_affinity = -1,
                },
            .usb =
                {
                    .init_usb_host_lib = true,
                    .task_stack = 4096,
                    .task_priority = 5,
                    .task_affinity = -1,
                },
        };

        esp_video_init_config_t video_config = {
            .usb_uvc = &usb_uvc_config,
        };

        camera_ = new EspVideo(video_config);
    }
#endif  // CONFIG_ESP_VIDEO_ENABLE_USB_UVC_VIDEO_DEVICE

public:
    ~EspVocat() {
        // Stop tasks
        if (charge_task_handle_ != nullptr) {
            vTaskDelete(charge_task_handle_);
        }
        if (touch_task_handle_ != nullptr) {
            vTaskDelete(touch_task_handle_);
        }
        if (imu_task_handle_ != nullptr) {
            vTaskDelete(imu_task_handle_);
        }
#if ESP_VOCAT_ENABLE_CAP_TOUCH_SENSOR
        if (touch_slider_task_handle_ != nullptr) {
            vTaskDelete(touch_slider_task_handle_);
            touch_slider_task_handle_ = nullptr;
        }
        if (touch_slider_handle_ != nullptr) {
            touch_slider_sensor_delete(touch_slider_handle_);
            touch_slider_handle_ = nullptr;
        }
        if (touch_button_handle_ != nullptr) {
            touch_button_sensor_delete(touch_button_handle_);
            touch_button_handle_ = nullptr;
        }
#endif

        // Delete objects
        delete charge_;
        delete cst816s_;
        delete display_;
        // Note: backlight_ (PwmBacklight) and camera_ (EspVideo) are not deleted here
        // because their base classes (Backlight, Camera) don't have virtual destructors.
        // Since EspVocat is a singleton that lives for the device lifetime, this is acceptable.

        // Remove GPIO ISR handler
        gpio_isr_handler_remove(TP_PIN_NUM_INT);
        if (emotion_reset_timer_ != nullptr) {
            esp_timer_stop(emotion_reset_timer_);
            esp_timer_delete(emotion_reset_timer_);
            emotion_reset_timer_ = nullptr;
        }

        // Disable temperature sensor
        if (temp_sensor != NULL) {
            temperature_sensor_disable(temp_sensor);
            temperature_sensor_uninstall(temp_sensor);
            temp_sensor = NULL;
        }
    }

    EspVocat() : boot_button_(BOOT_BUTTON_GPIO) {
        const esp_timer_create_args_t emotion_timer_args = {
            .callback = &EspVocat::emotion_reset_timer_callback,
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "emotion_rst",
            .skip_unhandled_events = true,
        };
        ESP_ERROR_CHECK(esp_timer_create(&emotion_timer_args, &emotion_reset_timer_));

        InitializeI2c();
        uint8_t pcb_version = DetectPcbVersion();
        InitializeCharge();
        InitializeCst816sTouchPad();
        InitializeBmi270();

        InitializeSpi();
        InitializeSt77916Display(pcb_version);
        InitializeButtons();
#if ESP_VOCAT_ENABLE_CAP_TOUCH_SENSOR
        InitializeCapacitiveTouchPads();
#endif
#ifdef CONFIG_ESP_VIDEO_ENABLE_USB_UVC_VIDEO_DEVICE
        InitializeCamera();
#endif  // CONFIG_ESP_VIDEO_ENABLE_USB_UVC_VIDEO_DEVICE
        InitializeTools();              // tool MCP self.screen.show_panel (brain điều khiển màn)
    }

    virtual AudioCodec* GetAudioCodec() override {
        static BoxAudioCodec audio_codec(
            i2c_bus_, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE, AUDIO_I2S_GPIO_MCLK,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR, AUDIO_CODEC_ES7210_ADDR,
            AUDIO_INPUT_REFERENCE);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override { return display_; }

    Cst816s* GetTouchpad() { return cst816s_; }

    virtual Backlight* GetBacklight() override { return backlight_; }

    virtual Camera* GetCamera() override { return camera_; }

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        if (charge_ == nullptr) {
            return false;
        }
        level = charge_->GetBatteryLevel();
        charging = charge_->IsCharging();
        discharging = charge_->IsDischarging();
        return true;
    }
};

DECLARE_BOARD(EspVocat);
