/*
 *  gst-tuio - Gstreamer to tuio computer vision plugin
 *
 *  Copyright (C) 2010 Keith Mok <ek9852@gmail.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>
#include <stdint.h>
#include "image_utils.h"

__attribute__((constructor)) static void image_util_iwmmxt_init( void );

/* TODO FIXME assume all src and dst are aligned, in iwmmxt src and dst need to be aligned */
/* use instruction walign to fix it !!! */

/* subtract an grayscale 8bits image (a-b), must same size !
 */
/* 640x480 x1000 times = ~4sec [~2.24X faster] */
static void
image8_subtract_iwmmxt(const guint8 *src1, const guint8 *src2, guint8 *dst, gint width, gint stride, gint height)
{
  while (height--) {
    const uint8_t *s1;
    const uint8_t *s2;
    uint8_t *d;
    int w;

    s1 = src1;
    s2 = src2;
    d = dst;
    w = width;

    while (w >= 32) {
      __asm__ volatile (
          "wldrd        wr0,   [%[s1]], #8\n\t"
          "wldrd        wr1,   [%[s1]], #8\n\t"
          "wldrd        wr2,   [%[s1]], #8\n\t"
          "wldrd        wr3,   [%[s1]], #8\n\t"
          "wldrd        wr4,   [%[s2]], #8\n\t"
          "wldrd        wr5,   [%[s2]], #8\n\t"
          "wldrd        wr6,   [%[s2]], #8\n\t"
          "wldrd        wr7,   [%[s2]], #8\n\t"

          "wsubbus      wr0, wr0, wr4\n\t"
          "wsubbus      wr1, wr1, wr5\n\t"
          "wsubbus      wr2, wr2, wr6\n\t"
          "wsubbus      wr3, wr3, wr7\n\t"

          "wstrd        wr0,   [%[d]], #8\n\t"
          "wstrd        wr1,   [%[d]], #8\n\t"
          "wstrd        wr2,   [%[d]], #8\n\t"
          "wstrd        wr3,   [%[d]], #8\n\t"
          : [s1] "+r" (s1), [s2] "+r" (s2), [d] "+r" (d)
          :
          : "memory",
          "wr0", "wr1", "wr2", "wr3",
          "wr4", "wr5", "wr6", "wr7");
      w -= 32;
    }
    while (w > 0) {
      gint32 v;
      v = *s1++ - *s2++;
      if (v < 0) v = 0;
      *d++ = v;
      w--;
    }
    src1 += stride;
    src2 += stride;
    dst += stride;
  }
}

/* 640x480 x1000 times = ~3.29sec [~2.6X faster] */
static void
image8_amplify_iwmmxt(const guint8 *src, guint8 *dst, gint width, gint stride, gint height, guint amplify_shift)
{
  while (height--) {
    const uint8_t *s;
    uint8_t *d;
    int w;;

    s = src;
    d = dst;
    w = width;

    while (w >= 16) {
      __asm__ volatile (
          "wxor         wr4, wr4, wr4\n\t"
          "tinsrb       wr4, %[amplify_shift], #0\n\t"

          "wldrd        wr0,    [%[s]], #8\n\t"
          "wldrd        wr2,    [%[s]], #8\n\t"
        
          "wunpckehub   wr1,    wr0\n\t"
          "wunpckelub   wr0,    wr0\n\t"
          "wunpckehub   wr3,    wr2\n\t"
          "wunpckelub   wr2,    wr2\n\t"

          "wmulul       wr0, wr0, wr0\n\t" /* self multiplication (max 255*255)*/
          "wmulul       wr1, wr1, wr1\n\t"
          "wmulul       wr2, wr2, wr2\n\t"
          "wmulul       wr3, wr3, wr3\n\t"
        
          "wsrlh      wr0, wr0, wr4\n\t"     /* amplify shift */
          "wsrlh      wr1, wr1, wr4\n\t"
          "wsrlh      wr2, wr2, wr4\n\t"
          "wsrlh      wr3, wr3, wr4\n\t"

          "wpackhus   wr0, wr0, wr1\n\t"
          "wpackhus   wr2, wr2, wr3\n\t"

          "wstrd        wr0,    [%[d]], #8\n\t"
          "wstrd        wr2,    [%[d]], #8\n\t"
          : [s] "+r" (s), [d] "+r" (d)
          : [amplify_shift] "r" (amplify_shift)
          : "memory",
          "wr0", "wr1", "wr2", "wr3", "wr4");
      w -= 16;
    }
    while (w > 0) {
      gint32 v;

      v = *s * *s;
      s++;
      v >>= amplify_shift;

      if (v >= 256) v = 255;
      *d++ = v;
      w--;
    }
    src += stride;
    dst += stride;
  }
}

/* 640x480 x1000 times = ~3sec [~2.6X faster] */
static void
image8_threshold_iwmmxt(const guint8 *src, guint8 *dst, gint width, gint stride, gint height, guint threshold)
{
  while (height--) {
    const uint8_t *s;
    uint8_t *d;
    int w;

    s = src;
    d = dst;
    w = width;

    while (w >= 32) {
      __asm__ volatile (
          "tbcstb    wr4, %[threshold]\n\t"

          "wldrd        wr0,   [%[s]], #8\n\t"
          "wldrd        wr1,   [%[s]], #8\n\t"
          "wldrd        wr2,   [%[s]], #8\n\t"
          "wldrd        wr3,   [%[s]], #8\n\t"

          "wcmpgtub     wr0, wr0, wr4\n\t"
          "wcmpgtub     wr1, wr1, wr4\n\t"
          "wcmpgtub     wr2, wr2, wr4\n\t"
          "wcmpgtub     wr3, wr3, wr4\n\t"

          "wstrd        wr0,   [%[d]], #8\n\t"
          "wstrd        wr1,   [%[d]], #8\n\t"
          "wstrd        wr2,   [%[d]], #8\n\t"
          "wstrd        wr3,   [%[d]], #8\n\t"
          : [s] "+r" (s), [d] "+r" (d)
          : [threshold] "r" (threshold)
          : "memory",
          "wr0", "wr1", "wr2", "wr3", "wr4");
      w -= 32;
    }
    while (w > 0) {
      if (*s++ > threshold)
        *d++ = 255;
      else
        *d++ = 0;
      w--;
    }
    src += stride;
    dst += stride;
  }
}

static void image_util_iwmmxt_init(void)
{
  pf_image8_amplify = image8_amplify_iwmmxt;
  pf_image8_subtract = image8_subtract_iwmmxt;
  pf_image8_threshold = image8_threshold_iwmmxt;
}

