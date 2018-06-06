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

/* MMX Reference
 * http://webster.cs.ucr.edu/AoA/Windows/HTML/TheMMXInstructionSeta2.html
 * http://www.tommesani.com/MMXArithmetic.html */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>
#include <stdint.h>
#include "image_utils.h"

__attribute__((constructor)) static void image_util_mmx_init( void );

/* subtract an grayscale 8bits image (a-b), must same size !
 */
static void
image8_subtract_mmx(const guint8 *src1, const guint8 *src2, guint8 *dst,
    gint width, gint stride, gint height)
{
  while (height--) {
    const uint8_t *s1;
    const uint8_t *s2;
    uint8_t *d;
    int w;;

    s1 = src1;
    s2 = src2;
    d = dst;
    w = width;

    while (w >= 32) {
      __asm__ volatile (
          "movq     (%[s1]),   %%mm0\n\t"
          "movq    8(%[s1]),   %%mm1\n\t"
          "movq   16(%[s1]),   %%mm2\n\t"
          "movq   24(%[s1]),   %%mm3\n\t"
          "movq     (%[s2]),   %%mm4\n\t"
          "movq    8(%[s2]),   %%mm5\n\t"
          "movq   16(%[s2]),   %%mm6\n\t"
          "movq   24(%[s2]),   %%mm7\n\t"

          "psubusb     %%mm4,   %%mm0\n\t"
          "psubusb     %%mm5,   %%mm1\n\t"
          "psubusb     %%mm6,   %%mm2\n\t"
          "psubusb     %%mm7,   %%mm3\n\t"

          "movq   %%mm0,     (%[d])\n\t"
          "movq   %%mm1,    8(%[d])\n\t"
          "movq   %%mm2,   16(%[d])\n\t"
          "movq   %%mm3,   24(%[d])\n\t"
          :
          : [s1] "r" (s1), [s2] "r" (s2), [d] "r" (d)
          : "memory",
          "%mm0", "%mm1", "%mm2", "%mm3",
          "%mm4", "%mm5", "%mm6", "%mm7");
      s1 += 32;
      s2 += 32;
      d += 32;
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
  __asm__ volatile ("emms\n\t");
}

static void
image8_amplify_mmx(const guint8 *src, guint8 *dst, gint width, gint stride,
    gint height, guint amplify_shift)
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
          "movd %[amplify_shift], %%mm4\n\t"
          "pxor       %%mm7,   %%mm7\n\t"

          "movd     (%[s]),   %%mm0\n\t"
          "movd    4(%[s]),   %%mm1\n\t"
          "movd    8(%[s]),   %%mm2\n\t"
          "movd   12(%[s]),   %%mm3\n\t"
        
          "punpcklbw  %%mm7,   %%mm0\n\t"
          "punpcklbw  %%mm7,   %%mm1\n\t" 
          "punpcklbw  %%mm7,   %%mm2\n\t"
          "punpcklbw  %%mm7,   %%mm3\n\t"
        
          "pmullw     %%mm0,   %%mm0\n\t" /* self multi. (max 255*255) */
          "pmullw     %%mm1,   %%mm1\n\t"
          "pmullw     %%mm2,   %%mm2\n\t"
          "pmullw     %%mm3,   %%mm3\n\t"

          "psrlw      %%mm4,   %%mm0\n\t"     /* amplify shift */
          "psrlw      %%mm4,   %%mm1\n\t"
          "psrlw      %%mm4,   %%mm2\n\t"
          "psrlw      %%mm4,   %%mm3\n\t"

          "packuswb   %%mm1,    %%mm0\n\t"
          "packuswb   %%mm3,    %%mm2\n\t"

          "movq       %%mm0,   (%[d])\n\t"
          "movq       %%mm2,  8(%[d])\n\t"
          :
          : [s] "r" (s), [d] "r" (d), [amplify_shift] "r" (amplify_shift)
          : "memory",
            "%mm0", "%mm1", "%mm2", "%mm3",
            "%mm4", "%mm5", "%mm6", "%mm7");
      s += 16;
      d += 16;
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
  __asm__ volatile ("emms\n\t");
}

static void
image8_threshold_mmx(const guint8 *src, guint8 *dst, gint width, gint stride,
    gint height, guint threshold)
{
  uint32_t threshold_64[2];

  threshold_64[0] = threshold | threshold << 8 | threshold << 16 |
    threshold << 24;

  threshold_64[1] = threshold | threshold << 8 | threshold << 16 |
    threshold << 24;

  while (height--) {
    const uint8_t *s;
    uint8_t *d;
    int w;

    s = src;
    d = dst;
    w = width;

    while (w >= 32) {
       uint32_t signxchg[2] = { 0x80808080, 0x80808080 };
      __asm__ volatile (
          "movq %[threshold], %%mm7\n\t"
          "movq %[signxchg],  %%mm6\n\t"
          "movq     (%[s]),   %%mm0\n\t"
          "movq    8(%[s]),   %%mm1\n\t"
          "movq   16(%[s]),   %%mm2\n\t"
          "movq   24(%[s]),   %%mm3\n\t"
          /* pcmp only support signless, xor sign bit */
          "pxor      %%mm6,   %%mm0\n\t" 
          "pxor      %%mm6,   %%mm1\n\t"
          "pxor      %%mm6,   %%mm2\n\t"
          "pxor      %%mm6,   %%mm3\n\t"
          "pcmpgtb   %%mm7,   %%mm0\n\t"
          "pcmpgtb   %%mm7,   %%mm1\n\t"
          "pcmpgtb   %%mm7,   %%mm2\n\t"
          "pcmpgtb   %%mm7,   %%mm3\n\t"
          "movq      %%mm0,     (%[d])\n\t"
          "movq      %%mm1,    8(%[d])\n\t"
          "movq      %%mm2,   16(%[d])\n\t"
          "movq      %%mm3,   24(%[d])\n\t"
          :
          : [s] "r" (s), [d] "r" (d), [threshold] "m" (threshold_64[0]),
            [signxchg] "m" (signxchg[0])
          : "memory",
            "%mm0", "%mm1", "%mm2", "%mm3",
            "%mm4", "%mm5", "%mm6", "%mm7");
      s += 32;
      d += 32;
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
  __asm__ volatile ("emms\n\t");
}

static void image_util_mmx_init(void)
{
  pf_image8_amplify = image8_amplify_mmx;
  pf_image8_subtract = image8_subtract_mmx;
  pf_image8_threshold = image8_threshold_mmx;
}

