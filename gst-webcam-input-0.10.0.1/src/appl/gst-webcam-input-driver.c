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

#include <glib.h>
#include <string.h>
#include <lo/lo.h>
#include "gst-webcam-input-driver.h"

/* TUIO watcher */
static guint tuio_watch_id;
static lo_server *tuio_server;

static GSList *driver_list;
static struct webcam_input_driver *cur_driver_funcs;
static void *cur_driver_priv;
static gboolean down_msg_sent;

static void
input_driver_set(const char *name)
{
  GSList *next;
  struct webcam_input_driver *driver_funcs;

  down_msg_sent = FALSE;

  if (cur_driver_funcs) {
    cur_driver_funcs->finalize(cur_driver_priv);
    cur_driver_funcs = NULL;
    cur_driver_priv = NULL;
  }

  if (!name)
    return;

  next = driver_list;

  while (next) {
    driver_funcs = (struct webcam_input_driver *)next->data;

    if (strcmp(driver_funcs->get_name(), name) == 0)
      break;
    next = g_slist_next(next);
  }

  if (!next) {
    g_warning("Request: %s, no match input driver found, fall back using 1st available input driver\n", name);
    next = driver_list;
  }

  cur_driver_funcs = next->data;

  cur_driver_priv = cur_driver_funcs->init();
  if (!cur_driver_priv) {
    g_warning("cannot init driver\n");
    cur_driver_priv = NULL;
  }
}

void
webcam_input_driver_shutdownlo()
{
  if (tuio_watch_id)
    g_source_remove(tuio_watch_id);
  tuio_watch_id = 0;

  if (tuio_server)
    lo_server_free(tuio_server);
  tuio_server = NULL;

  input_driver_set(NULL);
}

static gboolean
lo_callback(GIOChannel *source, GIOCondition condition, gpointer data) 
{
  lo_server *s=(lo_server *) data;
  lo_server_recv_noblock(s, 0);
  
  return TRUE;
}

static void
handle_st_set(int tracking_id, float x, float y)
{
  if (down_msg_sent) {
    cur_driver_funcs->u.st.mouse_move(cur_driver_priv, x, y);
  } else {
    cur_driver_funcs->u.st.mouse_move(cur_driver_priv, x, y);
    cur_driver_funcs->u.st.mouse_down(cur_driver_priv, 0);
    down_msg_sent = TRUE;
  }
}

static void
handle_mt_set(int tracking_id, float x, float y)
{
  cur_driver_funcs->u.mt.touch(cur_driver_priv, tracking_id, x, y);

  if (!down_msg_sent) {
    down_msg_sent = TRUE;
  }
}

static void
handle_st_alive(int count, lo_arg **argv)
{
  /* sent mouse up event */
  if ((count == 0) && (down_msg_sent)) {
    down_msg_sent = FALSE;
    cur_driver_funcs->u.st.mouse_up(cur_driver_priv, 0);
  }
}

static void
handle_mt_alive(int count, lo_arg **argv)
{
  /* sent mouse up event */
  if (count == 0)
  if ((count == 0) && (down_msg_sent)) {
    down_msg_sent = FALSE;
    cur_driver_funcs->u.mt.touch_up(cur_driver_priv);
  }
}

static int
tuio2Dcur_handler(const char *path, const char *types, lo_arg **argv,
                    int argc, void *data, void *user)
{
  if (!cur_driver_priv)
    return 0;

  if (!strcmp((char *) argv[0],"set") && (argc > 5)) {
    if (cur_driver_funcs->support_mt)
      handle_mt_set(argv[1]->i, argv[2]->f, argv[3]->f);
    else
      handle_st_set(argv[1]->i, argv[2]->f, argv[3]->f);
  } else if (!strcmp((char *) argv[0],"alive") ) {
    if (cur_driver_funcs->support_mt)
      handle_mt_alive(argc-1, &argv[1]);
    else
      handle_st_alive(argc-1, &argv[1]);
  }
  return 0;
}

static void
errorlo(int num, const char *msg, const char *path)
{
  g_error("liblo server error %d in path %s: %s\n", num, path, msg);
}

int
webcam_input_driver_initlo(const char *port, const char *driver_name)
{
  int lo_fd;
  GIOChannel *ioc;

  webcam_input_driver_shutdownlo();
  tuio_server  = lo_server_new(port, errorlo);

  lo_server_add_method(tuio_server, "/tuio/2Dcur", NULL, tuio2Dcur_handler, NULL);

  /* get the file descriptor of the server socket, if supported */
  lo_fd = lo_server_get_socket_fd(tuio_server);

  if (lo_fd < 0) {
    g_error("cannto get lo socket");
  }

#ifdef G_OS_WIN32
  ioc = g_io_channel_win32_new_socket(lo_fd);
#else
  ioc = g_io_channel_unix_new(lo_fd);
#endif
  tuio_watch_id = g_io_add_watch(ioc, (GIOCondition)(G_IO_IN | G_IO_PRI), lo_callback, (gpointer) tuio_server);
  g_io_channel_unref(ioc);

  input_driver_set(driver_name);
  return lo_server_get_port(tuio_server);
}

void
webcam_input_driver_register(struct webcam_input_driver *driver_funcs)
{
  driver_list = g_slist_append(driver_list, driver_funcs);
}

void
webcam_input_driver_unregister(struct webcam_input_driver *driver_funcs)
{
  driver_list = g_slist_remove(driver_list, driver_funcs);

  if (driver_funcs == cur_driver_funcs)
    cur_driver_funcs = NULL;
}

