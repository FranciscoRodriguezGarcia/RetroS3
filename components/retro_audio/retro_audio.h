#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#define RETRO_AUDIO_RATE 22050
bool retro_audio_init(void);
bool retro_audio_connected(void);
void retro_audio_discard_pending(void);
void retro_audio_submit(const int16_t *samples, size_t count);
/* Called by the sole USB reader, alongside the legacy controls parser. */
bool retro_audio_receive(uint8_t byte, uint8_t *type, uint8_t *value);
