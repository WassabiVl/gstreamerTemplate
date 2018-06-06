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
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>

#define SINGLE_TOUCH_DRIVER
#include "input-driver.h"

WEBCAM_INPUT_DRIVER(x11_input);

struct x11_driver_private {
  Display *display;
  Window root;
};

static const char * 
webcam_input_driver_get_name(void)
{
  return "X11 single touch driver";
}

static void
webcam_input_driver_finalize(void *priv)
{
  Display *display;
  display = ((struct x11_driver_private *)priv)->display;
  if (display)
    XCloseDisplay(display);
  g_free(priv);
}

static gboolean
is_xtext_available(Display *display)
{
  int major_opcode, first_event, first_error;
  int event_basep, error_basep, majorp, minorp;
  gboolean available;

  /* check if XTest is available */
  available = XQueryExtension(display, XTestExtensionName, &major_opcode, &first_event, &first_error);

  g_debug("XQueryExtension(XTEST) returns major_opcode = %d, first_event = %d, first_error = %d",
      major_opcode, first_event, first_error);

  if (available) {
    /* check if XTest version is OK */
    XTestQueryExtension(display, &event_basep, &error_basep, &majorp, &minorp);
    g_debug("XTestQueryExtension returns event_basep = %d, error_basep = %d, majorp = %d, minorp = %d",
        event_basep, error_basep, majorp, minorp);
    if (majorp < 2 || (majorp == 2 && minorp < 2)) {
      /* bad version*/
      g_message("XTEST version is %d.%d \n", majorp, minorp);
      if (majorp == 2 && minorp == 1) {
        g_message("XTEST is 2.1 - no grab is available\n");
      } else {
        available = FALSE;
      }
    } else {
      /* allow XTest calls even if someone else has the grab; e.g. during
       * a window resize operation. Works only with XTEST2.2*/
      XTestGrabControl(display, True);
    }
  } else {
    g_warning("XTEST extension is unavailable");
  }

  return available;
}

static void *
webcam_input_driver_init(void)
{
  struct x11_driver_private *priv;
  priv = g_malloc0(sizeof(struct x11_driver_private));

  priv->display = XOpenDisplay(0);
  priv->root = DefaultRootWindow(priv->display);

  if ((priv->display == 0) || (priv->root == 0))
    goto out;

  if (!is_xtext_available(priv->display))
    goto out;

  return priv;

out:
  g_free(priv);
  return 0;
}

static void
webcam_input_driver_mouse_down(void *priv, int button)
{
  Display *display;
  display = ((struct x11_driver_private *)priv)->display;
  XTestFakeButtonEvent(display, button+1, True, CurrentTime);
  XSync(display, False);
}

static void
webcam_input_driver_mouse_up(void *priv, int button)
{
  Display *display;
  display = ((struct x11_driver_private *)priv)->display;
  XTestFakeButtonEvent(display, button+1, False, CurrentTime);
  XSync(display, False);
}

static void
webcam_input_driver_mouse_move(void *priv, int x, int y)
{
  Display *display;
  Window root = ((struct x11_driver_private *)priv)->root;
  display = ((struct x11_driver_private *)priv)->display;
  XWarpPointer(display, None, root, 0, 0, 0, 0, x, y);
  XSync(display, False);
}

