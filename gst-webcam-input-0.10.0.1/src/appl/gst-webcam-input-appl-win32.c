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
#include <gst/gst.h>
#include "gst-webcam-input-cali.h"
#include "gst-webcam-input-conf.h"

static GtkWidget *main_window = NULL;

int
main (int argc, char *argv[])
{
  struct webcam_input_conf *c;

  gst_init (&argc, &argv);
  gtk_init (&argc, &argv);

  c = webcam_input_load_conf();
  main_window = webcam_input_create_calibration_window (c);

  gtk_main ();

  webcam_input_finalize_conf(c);

  return 0;
}
