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

#include <gtk/gtk.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>
#include <winuser.h>

#define SINGLE_TOUCH_DRIVER
#include "input-driver.h"

WEBCAM_INPUT_DRIVER(win_st);

struct win_driver_private {
  int dummy;
};

static const char * 
webcam_input_driver_get_name(void)
{
  return "win32 single touch driver";
}

static void
webcam_input_driver_finalize(void *priv)
{
  g_free(priv);
}

static void *
webcam_input_driver_init()
{
  struct win_driver_private *priv;

  priv = g_malloc0(sizeof(struct win_driver_private));

  return priv;
}

static void
webcam_input_driver_mouse_down(void *priv, int button)
{
  INPUT input;

  input.type = INPUT_MOUSE;
  input.mi.dx = 0;
  input.mi.dy = 0;

  input.mi.mouseData = 0;

  switch (button) {
    case 0:
    default:
      input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_LEFTDOWN;
      break;
    case 1:
      input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_RIGHTDOWN;
      break;
  }

  input.mi.time = 0;
  input.mi.dwExtraInfo = 0;

  SendInput(1, &input, sizeof(INPUT));
}

static void
webcam_input_driver_mouse_up(void *priv, int button)
{
  INPUT input;

  input.type = INPUT_MOUSE;
  input.mi.dx = 0;
  input.mi.dy = 0;

  input.mi.mouseData = 0;

  switch (button) {
    case 0:
    default:
      input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_LEFTUP;
      break;
    case 1:
      input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_RIGHTUP;
      break;
  }

  input.mi.time = 0;
  input.mi.dwExtraInfo = 0;

  SendInput(1, &input, sizeof(INPUT));
}

static void
webcam_input_driver_mouse_move(void *priv, int x, int y)
{
  INPUT input;
  int screen_width, screen_height;

  screen_width = GetSystemMetrics(SM_CXSCREEN);
  screen_height = GetSystemMetrics(SM_CYSCREEN);

  input.type = INPUT_MOUSE;
  input.mi.dx = x*65536/screen_width;
  input.mi.dy = y*65536/screen_height;

  input.mi.mouseData = 0;
  input.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;
  input.mi.time = 0;
  input.mi.dwExtraInfo = 0;

  SendInput(1, &input, sizeof(INPUT));
}

