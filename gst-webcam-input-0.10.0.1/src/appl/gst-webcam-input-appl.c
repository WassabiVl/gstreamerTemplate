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
#include <unique/unique.h>
#include <gst/gst.h>
#include "gst-webcam-input-cali.h"
#include "gst-webcam-input-conf.h"

enum
{
  COMMAND_0, /* unused: 0 is an invalid command */

  COMMAND_CALI
};

static GtkWidget *main_window = NULL;

static UniqueResponse
message_received_cb (UniqueApp         *app,
    UniqueCommand      command,
    UniqueMessageData *message,
    guint              time_,
    gpointer           user_data)
{
  UniqueResponse res;

  switch (command)
  {
    case UNIQUE_ACTIVATE:
      /* move the main window to the screen that sent us the command */
      gtk_window_set_screen (GTK_WINDOW (main_window), unique_message_data_get_screen (message));
      gtk_window_present_with_time (GTK_WINDOW (main_window), time_);
      res = UNIQUE_RESPONSE_OK;
      break;
    case COMMAND_CALI:
        webcam_input_start_calibrate (main_window);
        res = UNIQUE_RESPONSE_OK;
      break;
    default:
      res = UNIQUE_RESPONSE_OK;
      break;
  }

  return res;
}

int
main (int argc, char *argv[])
{
  UniqueApp *app;

  gst_init (&argc, &argv);
  gtk_init (&argc, &argv);

  /* as soon as we create the UniqueApp instance we either have the name
   * we requested ("org.mydomain.MyApplication", in the example) or we
   * don't because there already is an application using the same name
   */
  app = unique_app_new_with_commands ("org.keithmok.gstwebcaminput", NULL,
      "cali", COMMAND_CALI,
      NULL);

  /* if there already is an instance running, this will return TRUE; there
   * is no race condition because the check is already performed at
   * construction time
   */
  if (unique_app_is_running (app))
  {
    UniqueResponse response; /* the response to our command */

    response = unique_app_send_message (app, UNIQUE_ACTIVATE, NULL);

    /* we don't need the application instance anymore */
    g_object_unref (app);

    if (response == UNIQUE_RESPONSE_OK)
      return 0;
    else
      g_error("send message to already launch application failed\n");
  }
  else
  {
    /* this is the first instance, so we can proceed with the usual application
     * construction sequence
     */
    struct webcam_input_conf *c;
    c = webcam_input_load_conf();
    main_window = webcam_input_create_calibration_window (c);

    /* the UniqueApp instance must "watch" all the top-level windows the application
     * creates, so that it can terminate the startup notification sequence for us
     */
    unique_app_watch_window (app, GTK_WINDOW (main_window));

    /* using this signal we get notifications from the newly launched instances
     * and we can reply to them; the default signal handler will just return
     * UNIQUE_RESPONSE_OK and terminate the startup notification sequence on each
     * watched window, so you can connect to the message-received signal only if
     * you want to handle the commands and responses
     */
    g_signal_connect (app, "message-received", G_CALLBACK (message_received_cb), NULL);

    gtk_main ();

    webcam_input_finalize_conf(c);

    /* don't forget to unref the object when cleaning up after the application
     * execution
     */
    g_object_unref (app);
  }

  return 0;
}
