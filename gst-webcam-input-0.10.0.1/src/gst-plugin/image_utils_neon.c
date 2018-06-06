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

__attribute__((constructor)) static void image_util_neon_init( void );

/* subtract an grayscale 8bits image (a-b), must same size !
 */
static void
image8_subtract_neon(const guint8 *src1, const guint8 *src2, guint8 *dst, gint width, gint stride, gint height)
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
          "vld1.64         {d0-d3},  [%[s1]]!\n\t"
          "vld1.64         {d4-d7},  [%[s2]]!\n\t"
          "vqsub.u8        q0,  q0, q2\n\t"
          "vqsub.u8        q1,  q1, q3\n\t"
          "vst1.64         {d0-d3}, [%[d]]!\n\t"
          : [s1] "+r" (s1), [s2] "+r" (s2), [d] "+r" (d)
          :
          : "memory",
          "q0", "q1", "q2", "q3");
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

static void
image8_amplify_neon(const guint8 *src, guint8 *dst, gint width, gint stride, gint height, guint amplify_shift)
{
  while (height--) {
    const uint8_t *s;
    uint8_t *d;
    int w;;

    s = src;
    d = dst;
    w = width;

    while (w >= 32) {
      __asm__ volatile (
          "vdup.8       q12, %[amplify_shift]\n\t"

          "vld1.64      {d0-d3},  [%[s]]!\n\t"

          "vmull.u8     q4, d0, d0\n\t"
          "vmull.u8     q5, d1, d1\n\t"
          "vmull.u8     q6, d2, d2\n\t"
          "vmull.u8     q7, d3, d3\n\t"

          "vshl.u16     q4, q4, q12\n\t" /* variable is negative so shift right ! */
          "vshl.u16     q5, q5, q12\n\t"
          "vshl.u16     q6, q6, q12\n\t"
          "vshl.u16     q7, q7, q12\n\t"

          "vshrn.i16     d0, q4, #0\n\t"
          "vshrn.i16     d1, q5, #0\n\t"
          "vshrn.i16     d2, q6, #0\n\t"
          "vshrn.i16     d3, q7, #0\n\t"

          "vst1.64      {d0-d3}, [%[d]]!\n\t"
          : [s] "+r" (s), [d] "+r" (d)
          : [amplify_shift] "r" (-amplify_shift)
          : "memory",
          "q0", "q1", "q2", "q3",
          "q4", "q5", "q6", "q7",
          "q8", "q9", "q10", "q11");
      w -= 32;
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

static void
image8_threshold_neon(const guint8 *src, guint8 *dst, gint width, gint stride, gint height, guint threshold)
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
          "vdup.8       q4, %[threshold]\n\t"

          "vld1.64      {d0-d3},  [%[s]]!\n\t"

          "vcgt.u8      q0, q0, q4\n\t"
          "vcgt.u8      q1, q1, q4\n\t"

          "vst1.64      {d0-d3}, [%[d]]!\n\t"
          : [s] "+r" (s), [d] "+r" (d)
          : [threshold] "r" (threshold)
          : "memory",
          "q0", "q1", "q2", "q3",
          "q4", "q5", "q6", "q7",
          "q8", "q9", "q10", "q11");
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

static inline void
blur(const guint8 *src, guint8 *dst, int w, int radius, int step)
{
	int x;
	const int length= radius*2 + 1;
	const int inv= ((1<<16) + length/2)/length;

	int sum= 0;

	for(x=0; x<radius; x++){
		sum+= src[x*step]<<1;
	}
	sum+= src[radius*step];

	for(x=0; x<=radius; x++){
		sum+= src[(radius+x)*step] - src[(radius-x)*step];
		dst[x*step]= (sum*inv + (1<<15))>>16;
	}

	for(; x<w-radius; x++){
		sum+= src[(radius+x)*step] - src[(x-radius-1)*step];
		dst[x*step]= (sum*inv + (1<<15))>>16;
	}

	for(; x<w; x++){
		sum+= src[(2*w-radius-x-1)*step] - src[(x-radius-1)*step];
		dst[x*step]= (sum*inv + (1<<15))>>16;
	}
}

static void
blur_vert(const guint8 *src, guint8 *dst, gint w, gint h, int stride, gint radius)
{
  int length;
  int inv;

  if (radius >= h)
    radius = h - 1;

  length = radius*2 + 1;
  inv = ((1<<16) + length/2)/length;

  /* CAUTION radius must be < w and < h */
  while(w>=8) {
      guint8 *s1, *s2;
      guint8 *d;

      __asm__ volatile(
          "vmov.i8      q3, #0\n\t" /* q3, q4 = sum */
          "vmov.i8      q4, #0\n\t"
          "vdup.32      q5, %[inv]\n\t" /* q5 = inv */

          "mov          %[s1], %[src]\n\t"
          "mov          %[d], %[dst]\n\t"

          /* for(y=0; y<radius; y++){
               sum+= src[y*step]<<1;
             }
             sum+= src[radius*step]; */
          "mov          r12,   %[radius]\n\t"

          "1:\n\t"
          "vld1.64      {d0},  [%[s1]]\n\t" /* load 8 pixels horizontially */
          "add          %[s1], %[s1], %[stride]\n\t"
          "vmovl.u8     q1, d0\n\t" /* extend to 16 bits */
          "vmovl.u16    q2, d3\n\t" /* extend to 32 bits */
          "vmovl.u16    q1, d2\n\t" /* extend to 32 bits */

          "vadd.u32     q4, q2, q4\n\t" /* q3, q4 = sum */
          "vadd.u32     q3, q1, q3\n\t"

          "subs         r12, r12, #1\n\t"
          "bne          1b\n\t"
        
          "vshl.u32     q3, q3, #1\n\t" /* sum += src[x*step]<<1 */
          "vshl.u32     q4, q4, #1\n\t"

          "vld1.64      {d0},  [%[s1]]\n\t"
          "vmovl.u8     q1, d0\n\t" /* extend to 16 bits */
          "vmovl.u16    q2, d3\n\t" /* extend to 32 bits */
          "vmovl.u16    q1, d2\n\t" /* extend to 32 bits */
          "vadd.u32     q4, q2, q4\n\t" /* q3, q4 = sum */
          "vadd.u32     q3, q1, q3\n\t"

          /* for(y=0; y<=radius; y++){
               sum+= src[(radius+y)*step] - src[(radius-y)*step];
               dst[y*step]= (sum*inv + (1<<15))>>16;
             } */
          /* s1 carried from above */
          "mov          %[s2], %[s1]\n\t"
          "add          r12,  %[radius], #1\n\t"
          "1:\n\t"
          "vld1.64      {d0},  [%[s1]]\n\t"
          "vmovl.u8     q1, d0\n\t" /* extend to 16 bits */
          "vmovl.u16    q2, d3\n\t" /* extend to 32 bits */
          "vmovl.u16    q1, d2\n\t" /* extend to 32 bits */
          "vadd.u32     q4, q2, q4\n\t" /* q3, q4 = sum */
          "vadd.u32     q3, q1, q3\n\t"

          "vld1.64      {d0},  [%[s2]]\n\t"
          "vmovl.u8     q1, d0\n\t" /* extend to 16 bits */
          "vmovl.u16    q2, d3\n\t" /* extend to 32 bits */
          "vmovl.u16    q1, d2\n\t" /* extend to 32 bits */
          "vsub.u32     q4, q4, q2\n\t" /* q3, q4 = sum */
          "vsub.u32     q3, q3, q1\n\t"

          "vmul.i32     q6, q3, q5\n\t" /* sum*inv */
          "vmul.i32     q7, q4, q5\n\t" /* sum*inv */
          "vrshrn.i32   d0, q6, #16\n\t"
          "vrshrn.i32   d1, q7, #16\n\t"
          "vmovn.i16    d0, q0\n\t"
          "vst1.64      {d0}, [%[d]]\n\t"

          "add         %[s1], %[s1] , %[stride]\n\t"
          "sub         %[s2], %[s2] , %[stride]\n\t"
          "add         %[d], %[d] , %[stride]\n\t"

          "subs         r12, r12, #1\n\t"
          "bne          1b\n\t"
      
          /* for(y=radius+1; y<h-radius; y++){
               sum+= src[(radius+y)*step] - src[(y-radius-1)*step];
               dst[y*step]= (sum*inv + (1<<15))>>16;
             } */
          /* s1 is carried from previous code */
          "mov          %[s2], %[src]\n\t"

          "sub          r12,  %[h], %[radius], lsl #1\n\t"
          "sub          r12,  r12, #1\n\t"
          "blt 2f\n\t"
          "1:\n\t"
          "vld1.64      {d0},  [%[s1]]\n\t"
          "vmovl.u8     q1, d0\n\t" /* extend to 16 bits */
          "vmovl.u16    q2, d3\n\t" /* extend to 32 bits */
          "vmovl.u16    q1, d2\n\t" /* extend to 32 bits */
          "vadd.u32     q4, q2, q4\n\t" /* q3, q4 = sum */
          "vadd.u32     q3, q1, q3\n\t"

          "vld1.64      {d0},  [%[s2]]\n\t"
          "vmovl.u8     q1, d0\n\t" /* extend to 16 bits */
          "vmovl.u16    q2, d3\n\t" /* extend to 32 bits */
          "vmovl.u16    q1, d2\n\t" /* extend to 32 bits */
          "vsub.u32     q4, q4, q2\n\t" /* q3, q4 = sum */
          "vsub.u32     q3, q3, q1\n\t"

          "vmul.i32     q6, q3, q5\n\t" /* sum*inv */
          "vmul.i32     q7, q4, q5\n\t" /* sum*inv */
          "vrshrn.i32   d0, q6, #16\n\t"
          "vrshrn.i32   d1, q7, #16\n\t"
          "vmovn.i16    d0, q0\n\t"
          "vst1.64      {d0}, [%[d]]\n\t"

          "add         %[s1], %[s1] , %[stride]\n\t"
          "add         %[s2], %[s2] , %[stride]\n\t"
          "add         %[d], %[d] , %[stride]\n\t"

          "subs         r12, r12, #1\n\t"
          "bne          1b\n\t"
      
          "2:\n\t"
	        /* for(h-radius; y<h; y++){
	           	sum+= src[(2*h-radius-y-1)*step] - src[(y-radius-1)*step];
	           	dst[y*step]= (sum*inv + (1<<15))>>16;
	           }*/
          /* s2 is carried from previous code */
          "cmp          %[h], %[radius]\n\t"
          "blt          2f\n\t"
          "mov          %[s1], %[src]\n\t"
          "mul          r12,  %[h], %[stride]\n\t"
          "sub          r12,  r12, %[stride]\n\t"
          "add          %[s1], r12\n\t"

          "mov          r12,  %[radius]\n\t"
          "1:\n\t"
          "vld1.64      {d0},  [%[s1]]\n\t"
          "vmovl.u8     q1, d0\n\t" /* extend to 16 bits */
          "vmovl.u16    q2, d3\n\t" /* extend to 32 bits */
          "vmovl.u16    q1, d2\n\t" /* extend to 32 bits */
          "vadd.u32     q4, q2, q4\n\t" /* q3, q4 = sum */
          "vadd.u32     q3, q1, q3\n\t"

          "vld1.64      {d0},  [%[s2]]\n\t"
          "vmovl.u8     q1, d0\n\t" /* extend to 16 bits */
          "vmovl.u16    q2, d3\n\t" /* extend to 32 bits */
          "vmovl.u16    q1, d2\n\t" /* extend to 32 bits */
          "vsub.u32     q4, q4, q2\n\t" /* q3, q4 = sum */
          "vsub.u32     q3, q3, q1\n\t"

          "vmul.i32     q6, q3, q5\n\t" /* sum*inv */
          "vmul.i32     q7, q4, q5\n\t" /* sum*inv */
          "vRshrn.i32   d0, q6, #16\n\t"
          "vRshrn.i32   d1, q7, #16\n\t"
          "vmovn.i16    d0, q0\n\t"
          "vst1.64      {d0}, [%[d]]\n\t"

          "sub         %[s1], %[s1] , %[stride]\n\t"
          "add         %[s2], %[s2] , %[stride]\n\t"
          "add         %[d], %[d] , %[stride]\n\t"

          "subs         r12, r12, #1\n\t"
          "bne          1b\n\t"
          "2:\n\t"
          : [s1] "=&r" (s1), [s2] "=&r" (s2), [d] "=&r" (d)
          : [src] "r" (src), [dst] "r" (dst), [h] "r" (h), [radius] "r" (radius), [stride] "r" (stride), [inv] "r" (inv)
          : "cc", "memory",
          "q0", "q1", "q2", "q3",
          "q4", "q5", "r12");
      src += 8;
      dst += 8;
      w -= 8;
  }
  while(w>0) {
      blur(src, dst, h, radius, stride);
      src++;
      dst++;
      w--;
  }
}

static void
blur_horiz(const guint8 *src, guint8 *dst, gint w, gint h, int stride, gint radius)
{
  int y;

  if (radius > w)
    radius = w - 1;

  /* TODO this is difficult to optimizate */
  for(y=0; y<h; y++){
    blur(src + y*stride, dst + y*stride, w, radius, 1);
  }
}

static void
image8_box_blur_neon(const guint8 *src, guint8 *dst, gint width, gint stride, gint height, guint8 *p, gint blur_radius)
{
  if(blur_radius<=0) {
    memcpy(dst,src,width*height); /* deal with degenerate kernel sizes */
    return;
  }
  blur_horiz(src, p, width, height, stride, blur_radius);
  blur_vert(p, dst, width, height, stride, blur_radius);
}

static void
update_background_buf_neon(const guint8 *src, guint8 *background, guint16 *background_fractional, gint width, gint stride, gint height)
{
  while (height--) {
    const uint8_t *s;
    uint8_t *b;
    int w;;

    s = src;
    b = background;
    w = width;

    while (w >= 16) {
      __asm__ volatile (
          "vdup.16       d12, %[_65529_]\n\t"
          "vdup.16       d13, %[_65536_65529_]\n\t"

          "vld1.64      {d0-d1},  [%[s]]!\n\t"
          "vld1.64      {d4-d5},  [%[b]]\n\t"
          "vld1.64      {d8-d11},  [%[background_fractional]]\n\t"

          "vmovl.u8     q1, d1\n\t" /* extend s to 16 bits */
          "vmovl.u8     q0, d0\n\t" /* extend s to 16 bits */

          "vmovl.u8     q3, d5\n\t" /* extend b to 16 bits */
          "vmovl.u8     q2, d4\n\t" /* extend b to 16 bits */

          "vmull.u16    q7, d4, d12\n\t" /* v = background * 65529 */
          "vmull.u16    q8, d5, d12\n\t"
          "vmull.u16    q9, d6, d12\n\t"
          "vmull.u16    q10, d7, d12\n\t"

          "vmull.u16    q11, d8, d12\n\t" /* background_fractional * 65529 */
          "vmull.u16    q12, d9, d12\n\t"
          "vmull.u16    q13, d10, d12\n\t"
          "vmull.u16    q14, d11, d12\n\t"

          "vshr.u32     q11, q11, #16\n\t" /* (background_fractional * 65529) >> 16 */
          "vshr.u32     q12, q12, #16\n\t"
          "vshr.u32     q13, q13, #16\n\t"
          "vshr.u32     q14, q14, #16\n\t"

          "vqadd.u32    q7, q7, q11\n\t" /* v += (background_fractional * 65529) >> 16 */
          "vqadd.u32    q8, q8, q12\n\t"
          "vqadd.u32    q9, q9, q13\n\t"
          "vqadd.u32    q10, q10, q14\n\t"

          "vmlal.u16    q7, d0, d13\n\t" /* v += s * (65536 - 65529) */
          "vmlal.u16    q8, d1, d13\n\t"
          "vmlal.u16    q9, d2, d13\n\t"
          "vmlal.u16    q10, d3, d13\n\t"

          "vshrn.u32   d0, q7, #16\n\t" /* convert result to background by >> 16 */
          "vshrn.u32   d1, q8, #16\n\t"
          "vshrn.u32   d2, q9, #16\n\t"
          "vshrn.u32   d3, q10, #16\n\t"

          "vqmovn.u16   d4, q0\n\t" /* saturate into 8 bits of background */
          "vqmovn.u16   d5, q1\n\t"

          "vst1.64      {d4-d5}, [%[b]]!\n\t"

          /* update background_fractional */
          "vmovn.i32    d0, q7\n\t"
          "vmovn.i32    d1, q8\n\t"
          "vmovn.i32    d2, q9\n\t"
          "vmovn.i32    d3, q10\n\t"
          "vst1.64      {d0-d3}, [%[background_fractional]]!\n\t"

          : [s] "+r" (s), [b] "+r" (b), [background_fractional] "+r" (background_fractional)
          : [_65529_] "r" (65529), [_65536_65529_] "r" (65536-65529)
          : "memory",
          "q0", "q1", "q2", "q3",
          "q4", "q5", "q6", "q7",
          "q8", "q9", "q10", "q11","q12","q13","q14");
      w -= 16;
    }
    while (w > 0) {
      gint32 v;

      v = *b;
      v *= 65529; /* (9999 * 6.5536) */
      v += ((((guint32)*background_fractional) * 65529) >> 16);
      v += ((guint32)*s++) * (65536 - 65529);
      if (v > (255<<16)) v = 255<<16;
      *b++ = v >> 16;
      *background_fractional++ = v & 0xFFFF;
      w--;
    }
    src += stride;
    background += stride;
  }
}

static void image_util_neon_init(void)
{
  pf_image8_amplify = image8_amplify_neon;
  pf_image8_subtract = image8_subtract_neon;
  pf_image8_threshold = image8_threshold_neon;
  pf_image8_box_blur = image8_box_blur_neon;
  pf_update_background_buf = update_background_buf_neon;
}

