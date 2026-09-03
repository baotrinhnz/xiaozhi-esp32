#pragma once

#include "display.h"
#include <memory>
#include <string>
#include <cstdint>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include "expression_emote.h"

namespace emote {

class EmoteDisplay : public Display {
public:
    EmoteDisplay(esp_lcd_panel_handle_t panel, esp_lcd_panel_io_handle_t panel_io, int width, int height);
    virtual ~EmoteDisplay();

    virtual void SetEmotion(const char* emotion) override;
    virtual void SetStatus(const char* status) override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void SetTheme(Theme* theme) override;
    virtual void ShowNotification(const char* notification, int duration_ms = 3000) override;
    virtual void UpdateStatusBar(bool update_all = false) override;
    virtual void SetPowerSaveMode(bool on) override;
    virtual void SetPreviewImage(const void* image);

    bool StopAnimDialog();
    bool InsertAnimDialog(const char* emoji_name, uint32_t duration_ms);

    // VoCat "panel mode": hiện ảnh JPEG (fetch từ NAS) đè lên mặt mèo; HidePanel() trả về mặt mèo.
    bool ShowPanelImage(const uint8_t* jpeg, size_t len);
    void HidePanel();
    bool IsPanelShown() const { return panel_shown_; }

    // VoCat media mode: ảnh nền full màn (bìa dim+gradient từ HomeCenter). CreateMediaCoverObj tạo obj RỖNG
    // TRƯỚC khi tạo label -> nền nằm DƯỚI chữ (z-order theo thứ tự tạo). ShowMediaCover nạp ảnh vào obj đó.
    void CreateMediaCoverObj();
    bool ShowMediaCover(const uint8_t* jpeg, size_t len);
    void HideMediaCover();

    // VoCat visualizer nhạc: hiện/refresh 1 ảnh RGB565 (board tự vẽ cột) đè lên nền media; HideViz ẩn đi.
    void ShowViz(const uint16_t* rgb565, int w, int h, int y_ofs);
    void HideViz();

    void RefreshAll();

    // Get emote handle for internal use
    emote_handle_t GetEmoteHandle() const { return emote_handle_; }

private:
    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;

    emote_handle_t emote_handle_ = nullptr;

    void* panel_img_ = nullptr;             // gfx_obj_t* của ảnh panel (tạo 1 lần, tái dùng)
    uint8_t* panel_rgb_ = nullptr;          // buffer RGB565 đã decode (giữ sống khi engine hiện)
    bool panel_shown_ = false;

    void* media_cover_img_ = nullptr;       // gfx_obj_t* của bìa media (tạo 1 lần, tái dùng)
    uint8_t* media_cover_rgb_ = nullptr;    // buffer RGB565 bìa (giữ sống khi engine hiện)
    void* media_viz_img_ = nullptr;         // gfx_obj_t* của ảnh visualizer nhạc (tạo 1 lần)
};

} // namespace emote
