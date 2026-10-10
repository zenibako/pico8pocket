#ifndef P8P_AUDIO_H
#define P8P_AUDIO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct p8p_audio p8p_audio_t;

p8p_audio_t *p8p_audio_create(uint8_t *ram);
void p8p_audio_destroy(p8p_audio_t *audio);
void p8p_audio_reset(p8p_audio_t *audio, uint8_t *ram);
int p8p_audio_sfx(p8p_audio_t *audio, int sfx, int channel,
                  int offset, int length);
void p8p_audio_music(p8p_audio_t *audio, int pattern, int fade_ms, int mask);
int p8p_audio_channel_sfx(const p8p_audio_t *audio, int channel);
int p8p_audio_channel_note(const p8p_audio_t *audio, int channel);
int p8p_audio_music_pattern(const p8p_audio_t *audio);
int p8p_audio_music_count(const p8p_audio_t *audio);
int p8p_audio_music_ticks(const p8p_audio_t *audio);
void p8p_audio_render(p8p_audio_t *audio, int16_t *stereo, size_t frames);
/* Advance music, notes and PCM by frames output frames without
 * synthesizing them (muted output). */
void p8p_audio_skip(p8p_audio_t *audio, size_t frames);
/* PCM output channel (serial 0x808): 8-bit unsigned samples at 5512.5 Hz.
 * Returns how many bytes were queued; the rest did not fit. */
int p8p_audio_pcm_push(p8p_audio_t *audio, const uint8_t *samples, int count);
int p8p_audio_pcm_queued(const p8p_audio_t *audio);
size_t p8p_audio_state_size(void);
int p8p_audio_save_state(const p8p_audio_t *audio, void *destination,
                         size_t size);
int p8p_audio_load_state(p8p_audio_t *audio, uint8_t *ram,
                         const void *source, size_t size);

#ifdef __cplusplus
}
#endif

#endif
