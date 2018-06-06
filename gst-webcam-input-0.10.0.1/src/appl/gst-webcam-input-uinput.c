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
#include <linux/input.h>
#include <linux/uinput.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>

#define SINGLE_TOUCH_DRIVER
#include "input-driver.h"

WEBCAM_INPUT_DRIVER(uinput_st);

struct uinput_driver_private {
  int fd;
};

static const char * 
webcam_input_driver_get_name(void)
{
  return "uinput single touch driver";
}

static void
webcam_input_driver_finalize(void *priv)
{
  int fd;
  fd = ((struct uinput_driver_private *)priv)->fd;
  if (fd >= 0)
    close(fd);
  g_free(priv);
}

static void *
webcam_input_driver_init()
{
  int ret;
  struct uinput_user_dev uinp;
  int screen_width, screen_height;
  GdkScreen *screen;
  struct uinput_driver_private *priv;

  screen = gdk_screen_get_default();
  screen_width = gdk_screen_get_width(screen);
  screen_height = gdk_screen_get_height(screen);

  priv = g_malloc0(sizeof(struct uinput_driver_private));

  /* TODO we need root permission to do that */
  ret = system("modprobe uinput");

  /* sleep let hald to create the device node */
  usleep(100*1000);

  priv->fd = open("/dev/uinput", O_WRONLY | O_NDELAY);

  g_warning ("Cannot open /dev/uinput\n");

  if (priv->fd < 0)
    goto out;

  memset(&uinp, 0, sizeof(uinp));
  strncpy(uinp.name, "webcam to mouse", 20);
  uinp.id.version = 4;
  uinp.id.bustype = BUS_USB;

  ioctl(priv->fd, UI_SET_EVBIT, EV_KEY);
  ioctl(priv->fd, UI_SET_KEYBIT, BTN_LEFT);
  ioctl(priv->fd, UI_SET_KEYBIT, BTN_RIGHT);
  ioctl(priv->fd, UI_SET_EVBIT, EV_ABS);
  ioctl(priv->fd, UI_SET_ABSBIT, ABS_X);
  ioctl(priv->fd, UI_SET_ABSBIT, ABS_Y);

  uinp.absmax[ABS_X] = screen_width;
  uinp.absmin[ABS_X] = 0;
  uinp.absmax[ABS_Y] = screen_height;
  uinp.absmin[ABS_Y] = 0;

  ret = write(priv->fd, &uinp, sizeof(uinp));

  ioctl(priv->fd, UI_DEV_CREATE);

  return priv;

out:
  g_free(priv);
  return 0;
}

static void
webcam_input_driver_mouse_down(void *priv, int button)
{
  int ret;
  int linux_event_code;
  struct input_event event;
  struct uinput_driver_private *driver_priv = (struct uinput_driver_private *)priv;

  switch (button) {
    case 0:
    default:
      linux_event_code = BTN_LEFT;
      break;
    case 1:
      linux_event_code = BTN_RIGHT;
      break;
  }

  gettimeofday(&event.time, NULL);
  event.type = EV_KEY;
  event.code = linux_event_code;
  event.value = 1;
  ret = write(driver_priv->fd, &event, sizeof(event));

  gettimeofday(&event.time, NULL);
  event.type = EV_SYN;
  event.code = SYN_REPORT;
  event.value = 0;
  ret = write(driver_priv->fd, &event, sizeof(event));
}

static void
webcam_input_driver_mouse_up(void *priv, int button)
{
  int ret;
  int linux_event_code;
  struct input_event event;
  struct uinput_driver_private *driver_priv = (struct uinput_driver_private *)priv;

  switch (button) {
    case 0:
    default:
      linux_event_code = BTN_LEFT;
      break;
    case 1:
      linux_event_code = BTN_RIGHT;
      break;
  }

  gettimeofday(&event.time, NULL);
  event.type = EV_KEY;
  event.code = linux_event_code;
  event.value = 0;
  ret = write(driver_priv->fd, &event, sizeof(event));

  gettimeofday(&event.time, NULL);
  event.type = EV_SYN;
  event.code = SYN_REPORT;
  event.value = 0;
  ret = write(driver_priv->fd, &event, sizeof(event));
}

static void
webcam_input_driver_mouse_move(void *priv, int x, int y)
{
  int ret;
  struct input_event event;
  struct uinput_driver_private *driver_priv = (struct uinput_driver_private *)priv;

  gettimeofday(&event.time, NULL);
  event.type = EV_ABS;
  event.code = ABS_X;
  event.value = x;
  ret = write(driver_priv->fd, &event, sizeof(event));

  gettimeofday(&event.time, NULL);
  event.type = EV_ABS;
  event.code = ABS_Y;
  event.value = y;
  ret = write(driver_priv->fd, &event, sizeof(event));

  gettimeofday(&event.time, NULL);
  event.type = EV_SYN;
  event.code = SYN_REPORT;
  event.value = 0;
  ret = write(driver_priv->fd, &event, sizeof(event));
}


