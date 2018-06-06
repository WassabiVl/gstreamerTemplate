/*
 *  gst-tuio - Gstreamer to tuio computer vision plugin
 *
 *  Copyright (C) 2010 keithmok <ek9852@gmail.com>
 * 
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "n-point-cal.h"

/* Least square solution of linear equation set
 * Reference material: Calibration in touch-screen systems from 
 * http://focus.ti.com/lit/an/slyt277/slyt277.pdf
 */

void
n_point_cal(gfloat *xk, gfloat *yk, gfloat *xk_a, gfloat *yk_a, gint n,
    gfloat *matrix)
{
  gint k;
  gfloat a, b, c, d, e;
  gfloat X1, X2, X3, Y1, Y2, Y3;
  gfloat det;
  gfloat det_x1, det_x2, det_x3;
  gfloat det_y1, det_y2, det_y3;

  a = b = c = d = e = 0;
  for(k=0;k<n;k++) {
    a += xk_a[k] * xk_a[k];
    b += yk_a[k] * yk_a[k];
    c += xk_a[k] * yk_a[k];
    d += xk_a[k];
    e += yk_a[k];
  }

  X1 = X2 = X3 = Y1 = Y2 = Y3 = 0;
  for(k=0;k<n;k++) {
    X1 += xk_a[k] * xk[k];
    Y1 += xk_a[k] * yk[k];
    X2 += yk_a[k] * xk[k];
    Y2 += yk_a[k] * yk[k];
    X3 += xk[k];
    Y3 += yk[k];
  }
  
  det = n*(a*b - c*c) + 2*c*d*e - a*e*e - b*d*d;
  det_x1 = n*(X1*b - X2*c) + e*(X2*d - X1*e) + X3*(c*e - b*d);
  det_x2 = n*(X2*a - X1*c) + d*(X1*e - X2*d) + X3*(c*d - a*e);
  det_x3 = X3*(a*b - c*c) + X1*(c*e - b*d) + X2*(c*d - a*e);
  det_y1 = n*(Y1*b - Y2*c) + e*(Y2*d - Y1*e) + Y3*(c*e - b*d);
  det_y2 = n*(Y2*a - Y1*c) + d*(Y1*e - Y2*d) + Y3*(c*d - a*e);
  det_y3 = Y3*(a*b - c*c) + Y1*(c*e - b*d) + Y2*(c*d - a*e);

  matrix[0] = det_x1/det;
  matrix[1] = det_x2/det;
  matrix[2] = det_x3/det;
  matrix[3] = det_y1/det;
  matrix[4] = det_y2/det;
  matrix[5] = det_y3/det;
}

