/*
 * Copyright (C) 2006-2011, Mark Grebe
 * Copyright (C) 2018-2023, Jens Guenther
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdlib.h>
#include <SDL_video.h>
#include "blit.h"

static Uint8 *blitMap;

static void CopyBlitImageTo1Byte(int width, int height, const Uint8 *src,
    int srcskip, Uint8 *dst, int dstskip, const Uint8 *map)
{
  while (height--) {
    Uint8 byte = 0;
    int c;

    for (c = 0; c < width; ++c) {
      if ((c & 7) == 0)
        byte = *src++;

      if ((byte & 0x80) >> 7)
        *dst = map[1];
      else
        *dst = map[0];

      byte <<= 1;
      dst++;
    }

    src += srcskip;
    dst += dstskip;
  }
}

static void CopyBlitImageTo2Byte(int width, int height, const Uint8 *src,
    int srcskip, Uint16 *dst, int dstskip, const Uint16 *map)
{
  while (height--) {
    Uint8 byte = 0;
    int c;

    for (c = 0; c < width; ++c) {
      if ((c & 7) == 0)
        byte = *src++;

      if ((byte & 0x80) >> 7)
        *dst = map[1];
      else
        *dst = map[0];

      byte <<= 1;
      dst++;
    }

    src += srcskip;
    dst += dstskip;
  }
}

static void CopyBlitImageTo3Byte(int width, int height, const Uint8 *src,
    int srcskip, Uint8 *dst, int dstskip, const Uint8 *map)
{
  while (height--) {
    Uint8 byte = 0;
    int c;

    for (c = 0; c < width; ++c) {
      if ((c & 7) == 0)
        byte = *src++;

      if ((byte & 0x80) >> 7) {
        dst[0] = map[3];
        dst[1] = map[4];
        dst[2] = map[5];
      } else {
        dst[0] = map[0];
        dst[1] = map[1];
        dst[2] = map[2];
      }

      byte <<= 1;
      dst += 3;
    }

    src += srcskip;
    dst += dstskip;
  }
}

static void CopyBlitImageTo4Byte(int width, int height, const Uint8 *src,
    int srcskip, Uint32 *dst, int dstskip, const Uint32 *map)
{
  while (height--) {
    Uint8 byte = 0;
    int c;

    for (c = 0; c < width; ++c) {
      if ((c & 7) == 0)
        byte = *src++;

      if ((byte & 0x80) >> 7)
        *dst = map[1];
      else
        *dst = map[0];

      byte <<= 1;
      dst++;
    }

    src += srcskip;
    dst += dstskip;
  }
}

static void XorBlitImageTo1Byte(int width, int height, const Uint8 *src,
    int srcskip, Uint8 *dst, int dstskip, const Uint8 *map)
{
  while (height--) {
    Uint8 byte = 0;
    int c;

    for (c = 0; c < width; ++c) {
      if ((c & 7) == 0)
        byte = *src++;

      if ((byte & 0x80) >> 7) {
        if (*dst == map[0])
          *dst = map[1];
        else
          *dst = map[0];
      }

      byte <<= 1;
      dst++;
    }

    src += srcskip;
    dst += dstskip;
  }
}

static void XorBlitImageTo2Byte(int width, int height, const Uint8 *src,
    int srcskip, Uint16 *dst, int dstskip, const Uint16 *map)
{
  while (height--) {
    Uint8 byte = 0;
    int c;

    for (c = 0; c < width; ++c) {
      if ((c & 7) == 0)
        byte = *src++;

      if ((byte & 0x80) >> 7) {
        if (*dst == map[0])
          *dst = map[1];
        else
          *dst = map[0];
      }

      byte <<= 1;
      dst++;
    }

    src += srcskip;
    dst += dstskip;
  }
}

static void XorBlitImageTo3Byte(int width, int height, const Uint8 *src,
    int srcskip, Uint8 *dst, int dstskip, const Uint8 *map)
{
  while (height--) {
    Uint8 byte = 0;
    int c;

    for (c = 0; c < width; ++c) {
      if ((c & 7) == 0)
        byte = *src++;

      if ((byte & 0x80) >> 7) {
        if ((dst[0] == map[0]) && (dst[1] == map[1]) && (dst[2] == map[2])) {
          dst[0] = map[3];
          dst[1] = map[4];
          dst[2] = map[5];
        } else {
          dst[0] = map[0];
          dst[1] = map[1];
          dst[2] = map[2];
        }
      }

      byte <<= 1;
      dst += 3;
    }

    src += srcskip;
    dst += dstskip;
  }
}

static void XorBlitImageTo4Byte(int width, int height, const Uint8 *src,
    int srcskip, Uint32 *dst, int dstskip, const Uint32 *map)
{
  while (height--) {
    Uint8 byte = 0;
    int c;

    for (c = 0; c < width; ++c) {
      if ((c & 7) == 0)
        byte = *src++;

      if ((byte & 0x80) >> 7) {
        if (*dst == map[0])
          *dst = map[1];
        else
          *dst = map[0];
      }

      byte <<= 1;
      dst++;
    }

    src += srcskip;
    dst += dstskip;
  }
}

void TrsBlitMap(SDL_Palette *src, SDL_PixelFormat *dst)
{
  Uint8 *map;
  int i;
  unsigned alpha;
  Uint32 mapValue;

  if (blitMap != NULL)
    free(blitMap);

  if (src == NULL || dst == NULL)
    return;

  map = (Uint8 *)malloc(src->ncolors * dst->BytesPerPixel);
  if (map == NULL) {
    return;
  }

  alpha = dst->Amask ? SDL_ALPHA_OPAQUE : 0;
  for (i = 0; i < src->ncolors; ++i) {

    mapValue = SDL_MapRGBA(dst,
        src->colors[i].r,
        src->colors[i].g,
        src->colors[i].b,
        alpha);

    switch (dst->BytesPerPixel) {
      case 1:
        map[i] = (Uint8)mapValue;
        break;
      case 2:
        *((Uint16 *)(&map[i * 2])) = (Uint16)mapValue;
        break;
      case 3:
        map[i * 3] = mapValue >> 16;
        map[i * 3 + 1] = mapValue >> 8;
        map[i * 3 + 2] = mapValue & 0xFF;
        break;
      case 4:
        *((Uint32 *)(&map[i * 4])) = (Uint32)mapValue;
        break;
    }
  }
  blitMap = map;
}


/* The general purpose software blit routine */
void TrsSoftBlit(SDL_Surface *src, SDL_Rect *srcrect,
                 SDL_Surface *dst, SDL_Rect *dstrect, int xor)
{
  /* Set up the blit information */
  int const rect_h  = srcrect->h;
  int const rect_w  = srcrect->w;
  int const bpp     = dst->format->BytesPerPixel;
  int const bit_pix = src->format->BitsPerPixel;
  int const srcskip = src->pitch - (rect_w * bit_pix) / 8;
  int const dstskip = dst->pitch -  rect_w * bpp;

  Uint8 const *srcpix = (Uint8 *)src->pixels +
      (Uint16)srcrect->y * src->pitch +
     ((Uint16)srcrect->x * bit_pix) / 8;

  Uint8 *dstpix = (Uint8 *)dst->pixels +
      (Uint16)dstrect->y * dst->pitch +
      (Uint16)dstrect->x * bpp;

  dstrect->h = rect_h;
  dstrect->w = rect_w;

  /* Lock the destination if it's in hardware */
  if (SDL_MUSTLOCK(dst)) {
    if (SDL_LockSurface(dst) < 0)
      return;
  }

  /* Run the actual software blit */
  switch(bpp) {
    case 1:
      if (xor)
        XorBlitImageTo1Byte(rect_w, rect_h, srcpix, srcskip,
            dstpix, dstskip, blitMap);
      else
        CopyBlitImageTo1Byte(rect_w, rect_h, srcpix, srcskip,
            dstpix, dstskip, blitMap);
      break;
    case 2:
      if (xor)
        XorBlitImageTo2Byte(rect_w, rect_h, srcpix, srcskip,
            (Uint16 *)dstpix, dstskip / 2, (Uint16 *)blitMap);
      else
        CopyBlitImageTo2Byte(rect_w, rect_h, srcpix, srcskip,
            (Uint16 *)dstpix, dstskip / 2, (Uint16 *)blitMap);
      break;
    case 3:
      if (xor)
        XorBlitImageTo3Byte(rect_w, rect_h, srcpix, srcskip,
            dstpix, dstskip, blitMap);
      else
        CopyBlitImageTo3Byte(rect_w, rect_h, srcpix, srcskip,
            dstpix, dstskip, blitMap);
      break;
    case 4:
      if (xor)
        XorBlitImageTo4Byte(rect_w, rect_h, srcpix, srcskip,
            (Uint32 *)dstpix, dstskip / 4, (Uint32 *)blitMap);
      else
        CopyBlitImageTo4Byte(rect_w, rect_h, srcpix, srcskip,
            (Uint32 *)dstpix, dstskip / 4, (Uint32 *)blitMap);
      break;
    default:
      break;
  }

  if (SDL_MUSTLOCK(dst))
    SDL_UnlockSurface(dst);
}
