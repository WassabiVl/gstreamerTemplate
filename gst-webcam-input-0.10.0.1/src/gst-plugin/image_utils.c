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
#include "image_utils.h"

static void
update_background_buf(const guint8 *s, guint8 *background,
    guint16 *background_fractional, gint width, gint stride, gint height);

static void
image8_box_blur(const guint8 *src, guint8 *dst, gint width, gint stride,
    gint height, guint8 *p, gint blur_radius);

static void
image8_subtract(const guint8 *a, const guint8 *b, guint8 *c, gint width,
    gint stride, gint height);

static void
image8_amplify(const guint8 *src, guint8 *dst, gint width, gint stride,
    gint height, guint amplify_shift);

static void
image8_threshold(const guint8 *src, guint8 *dst, gint width, gint stride,
    gint height, guint threshold);

/* default function pointers */
update_background_buf_t pf_update_background_buf = update_background_buf;
image8_box_blur_t pf_image8_box_blur = image8_box_blur;
image8_subtract_t pf_image8_subtract = image8_subtract;
image8_amplify_t pf_image8_amplify = image8_amplify;
image8_threshold_t pf_image8_threshold = image8_threshold;

/* Reference: 
 * blur algorithm from Four Tricks for Fast Blurring in Software and Hardware
 *   http://www.gamasutra.com/features/20010209/Listing2.cpp
 *   http://www.gamasutra.com/features/20010209/Listing3.cpp */
static inline void
blur(const guint8 *src, guint8 *dst, int w, int radius, int step)
{
  int x;
  const int length = radius*2 + 1;
  const int inv = ((1<<16) + length/2)/length;

  int sum= 0;

  for (x = 0; x < radius; x++) {
    sum += src[x*step]<<1;
  }
  sum += src[radius*step];

  for (x = 0; x <= radius; x++) {
    sum += src[(radius+x)*step] - src[(radius-x)*step];
    dst[x*step]= (sum*inv + (1<<15))>>16;
  }

  for (; x < w-radius; x++) {
    sum += src[(radius+x)*step] - src[(x-radius-1)*step];
    dst[x*step]= (sum*inv + (1<<15))>>16;
  }

  for (; x < w; x++) {
    sum += src[(2*w-radius-x-1)*step] - src[(x-radius-1)*step];
    dst[x*step]= (sum*inv + (1<<15))>>16;
  }
}

static void
blur_horiz(const guint8 *src, guint8 *dst, gint w, gint h, int stride,
    gint radius)
{
  int y;

  if (radius > w)
    radius = w - 1;

  for (y = 0; y < h; y++) {
    blur(src + y*stride, dst + y*stride, w, radius, 1);
  }
}

static void
blur_vert(const guint8 *src, guint8 *dst, gint w, gint h, int stride,
    gint radius)
{
  int x;

  if (radius >= h)
    radius = h - 1;

  for (x = 0; x < w; x++) {
    blur(src + x, dst + x, h, radius, stride);
  }
}

static void
image8_box_blur(const guint8 *src, guint8 *dst, gint width, gint stride,
    gint height, guint8 *p, gint blur_radius)
{
  if (blur_radius <= 0) {
    memcpy(dst,src,width*height); /* deal with degenerate kernel sizes */
    return;
  }
  blur_horiz(src, p, width, height, stride, blur_radius);
  blur_vert(p, dst, width, height, stride, blur_radius);
}

static void
update_background_buf(const guint8 *s, guint8 *background,
    guint16 *background_fractional, gint width, gint stride, gint height)
{
  gint i, j;
  guint32 v;
  for (j = 0; j < height; j++) {
    for (i = 0; i < width; i++) {
      v = *background;
      v *= 65529; /* (9999 * 6.5536) */
      v += ((((guint32)*background_fractional) * 65529) >> 16);
      v += ((guint32)*s++) * (65536 - 65529);
      if (v > (255<<16)) v = 255<<16;
      *background++ = v >> 16;
      *background_fractional++ = v & 0xFFFF;
    }
  }
}


/* subtract an grayscale 8bits image (a-b), must same size !
 */
static void
image8_subtract(const guint8 *a, const guint8 *b, guint8 *c, gint width,
    gint stride, gint height)
{
  gint i, j;
  gint32 v;

  for (i = 0; i < height; i++) {
    for (j = 0; j < width; j++) {
      v = *a++ - *b++;
      if (v < 0) v = 0;
      *c++ = v;
    }
  }
}

static void
image8_amplify(const guint8 *src, guint8 *dst, gint width, gint stride,
    gint height, guint amplify_shift)
{
  gint i, j;
  gint32 v;

  for (i = 0; i < height; i++) {
    for (j = 0; j < width; j++) {
      v = *src * *src;
      src++;
      v >>= amplify_shift;
      if (v >= 255) v = 255;
      *dst++ = v;
    }
  }
}

static void
image8_threshold(const guint8 *src, guint8 *dst, gint width, gint stride,
    gint height, guint threshold)
{
  gint i, j;

  for (i = 0; i < height; i++) {
    for (j = 0; j < width; j++) {
      if (*src++ > threshold)
        *dst++ = 255;
      else
        *dst++ = 0;
    }
  }
}

