#include "emote_display.h"

// Standard C++ headers
#include <cstring>
#include <memory>
#include <unordered_map>
#include <tuple>
#include <algorithm>
#include <cinttypes>

// Standard C headers
#include <sys/time.h>
#include <time.h>

// ESP-IDF headers
#include <esp_log.h>
#include <esp_lcd_panel_io.h>
#include <esp_timer.h>
#include <lvgl.h>

// FreeRTOS headers
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Project headers
#include "assets/lang_config.h"
#include "assets.h"
#include "board.h"
#include "gfx.h"
#include "expression_emote.h"
#include "jpeg_to_image.h"


namespace emote {

// ============================================================================
// Constants and Type Definitions
// ============================================================================

static const char* TAG = "EmoteDisplay";

// ============================================================================
// Forward Declarations
// ============================================================================

class EmoteDisplay;

// ============================================================================
// Helper Functions
// ============================================================================

static bool OnFlushIoReady(const esp_lcd_panel_io_handle_t panel_io,
    esp_lcd_panel_io_event_data_t* const edata, void* user_ctx)
{
    emote_handle_t handle = static_cast<emote_handle_t>(user_ctx);
    if (handle) {
        emote_notify_flush_finished(handle);
    }
    return true;
}

// Flush callback for emote
static void OnFlushCallback(int x_start, int y_start, int x_end, int y_end, const void* data, emote_handle_t handle)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)emote_get_user_data(handle);
    if (panel != nullptr) {
        esp_lcd_panel_draw_bitmap(panel, x_start, y_start, x_end, y_end, data);
    }
}

// ============================================================================
// Graphics Initialization Functions
// ============================================================================

static emote_handle_t InitializeEmote(const esp_lcd_panel_handle_t panel, const int width, const int height)
{
    if (!panel) {
        ESP_LOGE(TAG, "Invalid panel");
        return nullptr;
    }

    emote_config_t emote_cfg = {
        .flags = {
            .swap = true,
            .double_buffer = true,
            .buff_dma = false,
        },
        .gfx_emote = {
            .h_res = width,
            .v_res = height,
            .fps = 30,
        },
        .buffers = {
            .buf_pixels = static_cast<size_t>(width * 16),
        },
        .task = {
            .task_priority = 5,
            .task_stack = 6 * 1024,
            .task_affinity = 0,
            .task_stack_in_ext = false,
        },
        .flush_cb = OnFlushCallback,
        .user_data = (void*)panel,
    };

    emote_handle_t emote_handle = emote_init(&emote_cfg);
    if (!emote_handle) {
        ESP_LOGE(TAG, "Failed to initialize emote");
        return nullptr;
    }

    return emote_handle;
}

// ============================================================================
// EmoteDisplay Class Implementation
// ============================================================================

EmoteDisplay::EmoteDisplay(const esp_lcd_panel_handle_t panel, const esp_lcd_panel_io_handle_t panel_io,
                           const int width, const int height)
{
    emote_handle_ = InitializeEmote(panel, width, height);

    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = OnFlushIoReady,
    };
    esp_lcd_panel_io_register_event_callbacks(panel_io, &cbs, emote_handle_);
}

EmoteDisplay::~EmoteDisplay()
{
    if (emote_handle_) {
        emote_deinit(emote_handle_);
        emote_handle_ = nullptr;
    }
}

void EmoteDisplay::SetEmotion(const char* const emotion)
{
    ESP_LOGI(TAG, "SetEmotion: %s", emotion);
    if (emote_handle_ && emotion && strlen(emotion) > 0) {
        emote_set_anim_emoji(emote_handle_, emotion);
    }
}

void EmoteDisplay::SetChatMessage(const char* const role, const char* const content)
{
    ESP_LOGI(TAG, "SetChatMessage: %s, %s", role, content);
    if (emote_handle_ && content && strlen(content) > 0) {
        if ((std::strcmp(role, "system") == 0) && std::strstr(content, "xiaozhi.me")) {
            size_t len = strlen(content);
            char* new_content = new char[len + 1];
            strcpy(new_content, content);
            std::replace(new_content, new_content + len, static_cast<char>(0x0A), static_cast<char>(0x20));
            emote_set_event_msg(emote_handle_, EMOTE_MGR_EVT_SYS, new_content);
            delete[] new_content;
        } else {
            emote_set_event_msg(emote_handle_, EMOTE_MGR_EVT_SPEAK, content);
        }
    }
}

void EmoteDisplay::SetStatus(const char* const status)
{
    ESP_LOGI(TAG, "SetStatus: %s", status);
    if (emote_handle_ && status && strlen(status) > 0) {
        if (std::strcmp(status, Lang::Strings::LISTENING) == 0) {
            emote_set_event_msg(emote_handle_, EMOTE_MGR_EVT_LISTEN, NULL);
        } else if (std::strcmp(status, Lang::Strings::STANDBY) == 0) {
            emote_set_event_msg(emote_handle_, EMOTE_MGR_EVT_IDLE, NULL);
        } else if (std::strcmp(status, Lang::Strings::SPEAKING) == 0) {
            emote_set_event_msg(emote_handle_, EMOTE_MGR_EVT_SPEAK, NULL);
        } else if (std::strcmp(status, Lang::Strings::ERROR) == 0) {
            emote_set_event_msg(emote_handle_, EMOTE_MGR_EVT_SET, NULL);
        }
    }
}

void EmoteDisplay::ShowNotification(const char* notification, int duration_ms)
{
    ESP_LOGI(TAG, "ShowNotification: %s", notification);
    if (emote_handle_ && notification && strlen(notification) > 0) {
        emote_set_event_msg(emote_handle_, EMOTE_MGR_EVT_SYS, notification);
    }
}

void EmoteDisplay::UpdateStatusBar(bool update_all)
{
    ESP_LOGD(TAG, "UpdateStatusBar: %s", update_all ? "true" : "false");
    if (!emote_handle_) {
        return;
    }
}

void EmoteDisplay::SetPowerSaveMode(bool on)
{
    ESP_LOGI(TAG, "SetPowerSaveMode: %s", on ? "ON" : "OFF");
    if (!emote_handle_) {
        return;
    }
}

void EmoteDisplay::SetPreviewImage(const void* image)
{
    if (image) {
        ESP_LOGI(TAG, "SetPreviewImage: Preview image not supported, using default icon");
    }
}

void EmoteDisplay::SetTheme(Theme* const theme)
{
    ESP_LOGI(TAG, "SetTheme: %p", theme);
}

bool EmoteDisplay::Lock(const int timeout_ms)
{
    (void)timeout_ms;
    return true;
}

void EmoteDisplay::Unlock()
{
}

bool EmoteDisplay::StopAnimDialog()
{
    ESP_LOGI(TAG, "StopAnimDialog");
    if (emote_handle_) {
        return emote_stop_anim_dialog(emote_handle_);
    }
    return false;
}

bool EmoteDisplay::InsertAnimDialog(const char* emoji_name, uint32_t duration_ms)
{
    ESP_LOGI(TAG, "InsertAnimDialog: %s, %" PRIu32, emoji_name, duration_ms);
    if (emote_handle_ && emoji_name) {
        return emote_insert_anim_dialog(emote_handle_, emoji_name, duration_ms);
    }
    return false;
}

bool EmoteDisplay::ShowPanelImage(const uint8_t* jpeg, size_t len)
{
    if (!emote_handle_ || !jpeg || !len) {
        return false;
    }
    // esp_emote_gfx 3.0.5 CHƯA có nguồn JPEG -> tự decode JPEG sang RGB565 rồi feed gfx_image_dsc_t.
    uint8_t* rgb = nullptr;
    size_t rgb_len = 0, w = 0, h = 0, stride = 0;
    if (jpeg_to_image(jpeg, len, &rgb, &rgb_len, &w, &h, &stride) != ESP_OK || rgb == nullptr) {
        ESP_LOGE(TAG, "ShowPanelImage: JPEG decode failed");
        return false;
    }
    emote_lock(emote_handle_);
    if (panel_rgb_) {
        free(panel_rgb_);                           // giải phóng ảnh trước
    }
    panel_rgb_ = rgb;                               // nhận sở hữu buffer RGB565 (giữ sống khi engine hiện)
    // jpeg_to_image ra RGB565 little-endian, gfx đọc byte ngược -> đảo màu (ám xanh). Hoán 2 byte/pixel.
    {
        uint16_t* px = reinterpret_cast<uint16_t*>(panel_rgb_);
        size_t npx = rgb_len / 2;
        for (size_t i = 0; i < npx; ++i) {
            px[i] = __builtin_bswap16(px[i]);
        }
    }
    static gfx_image_dsc_t s_dsc;                   // 1 panel image duy nhất -> static ổn
    s_dsc.header.magic = C_ARRAY_HEADER_MAGIC;      // 0x19 — decoder nhận ảnh raw qua magic này (BẮT BUỘC)
    s_dsc.header.flags = 0;
    s_dsc.header.cf = GFX_COLOR_FORMAT_RGB565;
    s_dsc.header.w = (uint16_t)w;
    s_dsc.header.h = (uint16_t)h;
    s_dsc.header.stride = (uint16_t)stride;
    s_dsc.data = panel_rgb_;
    s_dsc.data_size = (uint32_t)rgb_len;
    if (panel_img_ == nullptr) {
        panel_img_ = emote_create_obj_by_type(emote_handle_, "image", "panel_img");
    }
    if (panel_img_ == nullptr) {
        emote_unlock(emote_handle_);
        ESP_LOGE(TAG, "ShowPanelImage: create image obj failed");
        return false;
    }
    gfx_obj_t* obj = (gfx_obj_t*)panel_img_;
    gfx_img_set_src(obj, &s_dsc);
    gfx_obj_set_size(obj, (uint16_t)w, (uint16_t)h);
    gfx_obj_align(obj, GFX_ALIGN_CENTER, 0, 0);
    emote_set_anim_visible(emote_handle_, false);   // giấu mặt mèo
    gfx_obj_set_visible(obj, true);                 // hiện ảnh panel
    emote_unlock(emote_handle_);
    emote_notify_all_refresh(emote_handle_);
    panel_shown_ = true;
    ESP_LOGI(TAG, "ShowPanelImage: %ux%u", (unsigned)w, (unsigned)h);
    return true;
}

void EmoteDisplay::HidePanel()
{
    if (!emote_handle_) {
        return;
    }
    emote_lock(emote_handle_);
    if (panel_img_) {
        gfx_obj_set_visible((gfx_obj_t*)panel_img_, false);
    }
    emote_set_anim_visible(emote_handle_, true);    // trả về mặt mèo
    emote_unlock(emote_handle_);
    emote_notify_all_refresh(emote_handle_);
    panel_shown_ = false;
}

// Bìa nhỏ cho màn media native: KHÔNG giấu mặt mèo (ShowMedia đã tự giấu),
// chỉ tạo/hiện 1 gfx image object ở đỉnh màn. HomeCenter đã resize sẵn (gfx image không scale được).
bool EmoteDisplay::ShowMediaCover(const uint8_t* jpeg, size_t len)
{
    if (!emote_handle_ || !jpeg || !len) {
        return false;
    }
    uint8_t* rgb = nullptr;
    size_t rgb_len = 0, w = 0, h = 0, stride = 0;
    if (jpeg_to_image(jpeg, len, &rgb, &rgb_len, &w, &h, &stride) != ESP_OK || rgb == nullptr) {
        ESP_LOGE(TAG, "ShowMediaCover: JPEG decode failed");
        return false;
    }
    emote_lock(emote_handle_);
    if (media_cover_rgb_) {
        free(media_cover_rgb_);
    }
    media_cover_rgb_ = rgb;
    {
        uint16_t* px = reinterpret_cast<uint16_t*>(media_cover_rgb_);
        size_t npx = rgb_len / 2;
        for (size_t i = 0; i < npx; ++i) {
            px[i] = __builtin_bswap16(px[i]);       // little-endian -> gfx đọc ngược
        }
    }
    static gfx_image_dsc_t s_cover_dsc;              // 1 bìa media duy nhất
    s_cover_dsc.header.magic = C_ARRAY_HEADER_MAGIC;
    s_cover_dsc.header.flags = 0;
    s_cover_dsc.header.cf = GFX_COLOR_FORMAT_RGB565;
    s_cover_dsc.header.w = (uint16_t)w;
    s_cover_dsc.header.h = (uint16_t)h;
    s_cover_dsc.header.stride = (uint16_t)stride;
    s_cover_dsc.data = media_cover_rgb_;
    s_cover_dsc.data_size = (uint32_t)rgb_len;
    if (media_cover_img_ == nullptr) {
        media_cover_img_ = emote_create_obj_by_type(emote_handle_, "image", "media_cover");
    }
    if (media_cover_img_ == nullptr) {
        emote_unlock(emote_handle_);
        ESP_LOGE(TAG, "ShowMediaCover: create image obj failed");
        return false;
    }
    gfx_obj_t* obj = (gfx_obj_t*)media_cover_img_;
    gfx_img_set_src(obj, &s_cover_dsc);
    gfx_obj_set_size(obj, (uint16_t)w, (uint16_t)h);
    gfx_obj_align(obj, GFX_ALIGN_TOP_MID, 0, 88);   // bìa nằm DƯỚI dòng subtitle của engine, label media nằm dưới bìa
    gfx_obj_set_visible(obj, true);
    emote_unlock(emote_handle_);
    emote_notify_all_refresh(emote_handle_);
    ESP_LOGI(TAG, "ShowMediaCover: %ux%u", (unsigned)w, (unsigned)h);
    return true;
}

void EmoteDisplay::HideMediaCover()
{
    if (!emote_handle_) {
        return;
    }
    emote_lock(emote_handle_);
    if (media_cover_img_) {
        gfx_obj_set_visible((gfx_obj_t*)media_cover_img_, false);
    }
    emote_unlock(emote_handle_);
    emote_notify_all_refresh(emote_handle_);
}

void EmoteDisplay::RefreshAll()
{
    if (emote_handle_) {
        emote_notify_all_refresh(emote_handle_);
        return;
    }
}

} // namespace emote