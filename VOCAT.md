# VoCat (Meo) — fork tuỳ biến của xiaozhi-esp32

> Đây là **fork riêng** cho thiết bị **ESP-VoCat / EchoEar** (ESP32-S3-WROOM-1 N16R16, board `espressif/esp-vocat`)
> — một con mèo AI tiếng Việt tên **Meo**, brain self-host trên NAS Synology (192.168.1.4).
> Nhánh làm việc: **`mww`** (tách từ upstream `8548a56a`). File này mô tả phần TUỲ BIẾN so với upstream.

## Tuỳ biến chính (khác upstream)

| Mảng | Nội dung | File |
|------|----------|------|
| **Wake word** | Wake tiếng Việt **"Meo Meo"** on-device (microWakeWord, thay 你好喵伴 tiếng Trung). Giữ mặt mèo + ngủ thật. | `main/audio/wake_words/micro_wake_word.{cc,h}`, `models/meo_meo.tflite` |
| **Panel mode** | Vuốt (CST816S) → tải ảnh trang do NAS vẽ (đồng hồ/lịch/thời tiết) hiện lên màn. Vuốt lên/tap → về mặt mèo. | `main/boards/espressif/esp-vocat/esp_vocat.cc`, `main/display/emote_display.{cc,h}` |
| **MCP tool** | `self.screen.show_panel(person, view)` — brain (server plugin) ra lệnh hiện lịch người chỉ định. | `esp_vocat.cc` `InitializeTools()` |
| **Shake** | IMU BMI270: lắc → emote `panic`. | `esp_vocat.cc` `imu_event_task` |
| **Emote pack** | 23 biểu cảm **vector** (nét) từ Espressif Emote Pack, thêm bằng cách bỏ `.eaf` vào `emoji/` + map trong `emote.json`. | `main/boards/espressif/esp-vocat/{emoji/*.eaf, assets/360_360/emote.json}` |
| **Build glue** | Thêm `esp_http_client` vào PRIV_REQUIRES; staging emote copy CẢ folder `emoji/`. | `main/CMakeLists.txt` |

## Build & flash

- CI: `.github/workflows/vocat.yml` (workflow_dispatch, container `espressif/idf:v6.0.2`).
```bash
gh workflow run vocat.yml --repo baotrinhnz/xiaozhi-esp32 --ref mww
```
- Artifact **`build/merged-binary.bin`** → flash **@0x0** (web.esphome.io / esptool-js) → Program → RST.

## Ghi chú kỹ thuật (bài học)

- **Panel ảnh (esp_emote_gfx 3.0.5):** khi feed ảnh raw RGB565 qua `gfx_image_dsc_t` phải set `header.magic = C_ARRAY_HEADER_MAGIC (0x19)` (thiếu → màn đen "no decoder matched"), và **đảo byte** RGB565 (`__builtin_bswap16`, vì `jpeg_to_image` ra little-endian còn gfx đọc ngược → ám xanh + sọc).
- **Emote asset:** phân vùng `assets` (spiffs) 8MB @0x800000; build tự pack `.eaf` trong `emoji/` + `emote.json` bằng `build_speaker_assets_bin` (component `espressif2022/esp_emote_assets`). `.eaf` tạo bằng tool web **Packer NEXT** (vector Lottie → nét), export ra bin rồi rã lấy `.eaf`.
- **Đẩy file lên fork:** luôn FETCH bản hiện tại từ nhánh trước khi sửa+push (gh api PUT ghi đè cả file — push từ checkout cũ sẽ xoá mất fix khác).

## Kiến trúc hệ thống (Meo nối gì)

```
ESP-VoCat (fork này) ── WebSocket ──▶ xiaozhi-full (brain, NAS): LLM Gemini / STT Groq / TTS Edge / voiceprint / plugin
        │                                     │ đọc family.db
        └──── HTTP /panel ảnh ──▶ HomeCenter (NAS :8080): vẽ ảnh trang + cấu hình per-person + lưu trữ
                                              ▲
                              vocat-reminder (NAS): nhắc lịch iCal + báo Gmail
```

Tài liệu as-built đầy đủ toàn hệ thống (brain, HomeCenter, NAS storage, deploy) nằm ở kho riêng của chủ dự án
(`VoCat/VoCat-AsBuilt-MASTER.md`). File này chỉ tóm phần firmware để người đọc repo nắm nhanh.

## Giấy phép
Kế thừa giấy phép của upstream `xinnan-tech/xiaozhi-esp32`.
