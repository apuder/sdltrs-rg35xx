#ifndef TRS_SDL_KEYMAP_H
#define TRS_SDL_KEYMAP_H

#ifdef __cplusplus
extern "C" {
#endif

#define TRS_SOFT_KEY_COUNT 52
#define TRS_SOFT_KEY_COLUMNS 13
#define TRS_SOFT_KEY_ROWS 4
#define TRS_SOFT_KEY_SHIFT 39

extern const char *trs_soft_key_names[TRS_SOFT_KEY_COUNT];
extern const int trs_soft_key_syms[TRS_SOFT_KEY_COUNT];
extern const char *trs_soft_key_names_shifted[TRS_SOFT_KEY_COUNT];
extern const int trs_soft_key_syms_shifted[TRS_SOFT_KEY_COUNT];

int trs_soft_key_symbol(int index, int shifted);
const char *trs_soft_key_label(int index, int shifted);

#ifdef __cplusplus
}
#endif

#endif /* TRS_SDL_KEYMAP_H */
