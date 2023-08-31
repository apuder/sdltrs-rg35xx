#ifndef _BLIT_H
#define _BLIT_H

extern void TrsSoftBlit(const SDL_Surface *src, const SDL_Rect *srcrect,
                              SDL_Surface *dst, SDL_Rect *dstrect, int xor);

extern void TrsBlitMap(const SDL_Palette *src, const SDL_PixelFormat *dst);

#endif /* _BLIT_H */
