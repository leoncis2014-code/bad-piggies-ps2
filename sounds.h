// Generado por tools/gen_sounds.py
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { const short* data; int samples; } SoundData;
enum { SND_CLICK, SND_PLACE, SND_GO, SND_WIN, SND_LOSE, SND_COUNT };
extern const SoundData SOUNDS[SND_COUNT];
#ifdef __cplusplus
}
#endif
