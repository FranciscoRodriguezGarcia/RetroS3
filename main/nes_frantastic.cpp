#include "nes_frantastic.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <memory>
#include <new>

#include "frantastic/nes/NES.hpp"
extern "C" {
#include "display.h"
#include "emulator_diag.h"
#include "input.h"
#include "retro_audio.h"
}
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr int kWidth = 256;
constexpr int kHeight = 224;
constexpr int kX = 32;
constexpr int kY = 8;
constexpr int64_t kFrameUs = 16667;
const char *TAG = "NES_FRANTASTIC";

void fillFrame(void *ctx, uint16_t *dst, int first, int lines, int width) {
    auto *frame = static_cast<const std::uint32_t *>(ctx);
    for (int y = 0; y < lines; ++y) {
        const std::uint32_t *src = frame + (first + y) * kWidth;
        for (int x = 0; x < width; ++x) {
            const std::uint32_t c = src[x]; // PPU: 0xAARRGGBB
            const std::uint16_t rgb565 =
                static_cast<std::uint16_t>(((c >> 8) & 0xF800u) |
                                           ((c >> 5) & 0x07E0u) |
                                           ((c >> 3) & 0x001Fu));
            dst[y * width + x] = __builtin_bswap16(rgb565);
        }
    }
}

std::uint8_t buttons() {
    const input_state_t s = input_get_state();
    std::uint8_t b = 0;
    if (s & INPUT_BUTTON_A)      b |= 1u << 0;
    if (s & INPUT_BUTTON_B)      b |= 1u << 1;
    if (s & INPUT_BUTTON_SELECT) b |= 1u << 2;
    if (s & INPUT_BUTTON_START)  b |= 1u << 3;
    if (s & INPUT_BUTTON_UP)     b |= 1u << 4;
    if (s & INPUT_BUTTON_DOWN)   b |= 1u << 5;
    if (s & INPUT_BUTTON_LEFT)   b |= 1u << 6;
    if (s & INPUT_BUTTON_RIGHT)  b |= 1u << 7;
    return b;
}
} // namespace

extern "C" bool nes_frantastic_start(const char *rom_path) {
    if (!rom_path) {
        emulator_diag_set(EMU_DIAG_ROM_INVALID, "Ruta NES Frantastic vacia");
        return false;
    }

    try {
        // PPU contains a 256x240 RGBA framebuffer. The SPIRAM malloc mode
        // must be enabled so its owning NES instance can live in PSRAM.
        auto nes = frantastic::nes::NES::fromFile(rom_path, RETRO_AUDIO_RATE);
        if (!nes) {
            emulator_diag_set(EMU_DIAG_MEMORY, "NES Frantastic sin memoria");
            return false;
        }

        ESP_LOGI(TAG, "Iniciando %s (heap interno: %u, PSRAM: %u)",
                 rom_path,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        retro_audio_discard_pending();

        auto audio = std::make_unique<float[]>(1024);
        auto pcm = std::make_unique<int16_t[]>(1024);
        std::uint32_t displayed = 0;
        int64_t fpsStart = esp_timer_get_time();
        char fpsText[20] = "FPS: --.-";
        while (true) {
            if (input_consume_menu_request()) {
                retro_audio_discard_pending();
                return true;
            }
            const int64_t frameStart = esp_timer_get_time();
            nes->setButtons(0, buttons());
            nes->runFrame();

            const auto &frame = nes->framebuffer();
            esp_err_t err = display_draw_rgb565_blocks(
                kX, kY, kWidth, kHeight, fillFrame,
                const_cast<std::uint32_t *>(frame.data()));
            if (err != ESP_OK) {
                emulator_diag_set(EMU_DIAG_CORE, "Pantalla NES Frantastic");
                ESP_LOGE(TAG, "Video: %s", esp_err_to_name(err));
                return false;
            }

            const std::size_t count = nes->readAudio(audio.get(), 1024);
            for (std::size_t i = 0; i < count; ++i) {
                const float clipped = std::max(-1.0f, std::min(1.0f, audio[i]));
                pcm[i] = static_cast<int16_t>(clipped * 32767.0f);
            }
            if (count) retro_audio_submit(pcm.get(), count);

            ++displayed;
            const int64_t now = esp_timer_get_time();
            if (now - fpsStart >= 1000000) {
                const double fps = displayed * 1000000.0 / (now - fpsStart);
                snprintf(fpsText, sizeof(fpsText), "FPS: %4.1f", fps);
                display_draw_text(4, 0, fpsText, 0xFFFF, 0x0000);
                ESP_LOGI(TAG, "%s", fpsText);
                displayed = 0;
                fpsStart = now;
            }
            const int64_t spent = esp_timer_get_time() - frameStart;
            if (spent < kFrameUs - 1000)
                vTaskDelay(pdMS_TO_TICKS((kFrameUs - spent) / 1000));
        }
    } catch (const std::bad_alloc &e) {
        emulator_diag_set(EMU_DIAG_MEMORY, "NES Frantastic sin RAM");
        ESP_LOGE(TAG, "Memoria: %s", e.what());
    } catch (const std::exception &e) {
        emulator_diag_set(EMU_DIAG_CORE, "Fallo NES Frantastic");
        ESP_LOGE(TAG, "Core: %s", e.what());
    }
    retro_audio_discard_pending();
    return false;
}
