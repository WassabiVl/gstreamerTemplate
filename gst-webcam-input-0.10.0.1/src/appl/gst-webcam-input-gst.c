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

#ifdef G_OS_UNIX
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#include "gst-webcam-input-gst.h"
#include "gst-webcam-input-conf.h"

static GstElement *pipeline;
static GstBus *bus;

static void
prompt_error_and_exit(const gchar *msg, ...)
{
  GtkWidget *dialog;
  char buffer[1024];
  va_list ap;

  va_start (ap, msg);
  vsnprintf(buffer, sizeof(buffer)-1, msg, ap);
  va_end(ap);

  dialog = gtk_message_dialog_new (NULL,
      GTK_DIALOG_DESTROY_WITH_PARENT,
      GTK_MESSAGE_ERROR,
      GTK_BUTTONS_CLOSE,
      "%s", buffer);


  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
  g_error("%s", buffer);
}

void
webcam_input_finalize_gst(void)
{
  if (pipeline) {
    gst_element_set_state (pipeline, GST_STATE_NULL);
    gst_object_unref (GST_OBJECT (pipeline));
  }
  pipeline = NULL;
  if (bus)
    gst_object_unref (GST_OBJECT (bus));
  bus = NULL;

}

static gboolean
cb_bus(GstBus * bus, GstMessage * message, gpointer data)
{

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:{
      GError *err;
      gchar *debug;
      gchar *str;

      gst_message_parse_error (message, &err, &debug);
      str = gst_element_get_name (message->src);
      prompt_error_and_exit("Error: %s, %s\n%s", str, err->message, debug);

      /* we will not be here anyway */
      g_free (str);
      g_error_free (err);
      g_free (debug);

      break;
    case GST_MESSAGE_EOS:
      /* end-of-stream  should not be here */
      break;
    default:
      break;
    }
  }
  return TRUE;
}

void
webcam_input_initgst(int portno, struct webcam_input_conf *conf)
{
  GstElement *blobtuio;
  GstElement *src;
  GstElement *ffmpegcolorspace;
  GstCaps *caps;
  char *port;

  webcam_input_finalize_gst();

  pipeline = gst_pipeline_new ("app");
#ifdef G_OS_WIN32
  /* TODO */
  src = gst_element_factory_make ("videotestsrc", "source");
// VideoProcAmpFlags VideoProcAmp_Flags_Auto
/*
IAMCameraControl *pCameraControl = NULL;
IAMVideoProcAmp *pProcAmp = NULL;
...
hr = pCameraControl->Set(CameraControl_Exposure, 0, CameraControl_Flags_Auto);
hr = pProcAmp->Set(VideoProcAmp_Gain, 30, VideoProcAmp_Flags_Manual);
*/
//  src = gst_element_factory_make ("ksvideosrc", "source");
  if (!src) {
    g_error("Cannot create ksvideosrc gstreamer element\n");
  }
#else
#ifdef V4L2_CID_EXPOSURE_AUTO_PRIORITY
  /* HACK XXX since gstreamer does not export V4L2_CID_EXPOSURE_AUTO_PRIORITY, we set it to constant framerate 1st */
  /* if not set, some cheap usb webcam output frame rate as low as 8 fps or lower in dark condition which is too slow */
  /* However not all usb webcam support this interface */
  int fd;
  struct v4l2_control cntl;
  cntl.id = V4L2_CID_EXPOSURE_AUTO_PRIORITY;
  cntl.value = 0;
  fd = open(conf->v4l2_devname, O_RDWR);
  if (fd >= 0) {
    ioctl(fd, VIDIOC_S_CTRL, &cntl);
    g_message("disabled V4L2_CID_EXPOSURE_AUTO_PRIORITY\n");
    close(fd);
  } else {
    g_warning("Cannot open v4l2 device:%s for setting V4L2_CID_EXPOSURE_AUTO_PRIORITY\n", conf->v4l2_devname);
  }
#endif
  src = gst_element_factory_make ("v4l2src", "source");
  if (!src) {
    g_error("Cannot create v4l2 gstreamer element\n");
  }
  if (conf->v4l2_devname) {
    g_object_set(src, "device", conf->v4l2_devname, NULL);
  }
#endif

  ffmpegcolorspace = gst_element_factory_make ("ffmpegcolorspace", "colorconvert");

  blobtuio = gst_element_factory_make ("blobstotuio", "blobtuio");

  if (!blobtuio) {
    g_error("Cannot create blobstotuio gstreamer element\n");
  }
  if (!pipeline || !ffmpegcolorspace) {
    g_error("Cannot create gstreamer element\n");
  }

  g_object_set(blobtuio, "learn-background-counter", conf->learn_background_counter,
                         "threshold", conf->threshold,
                         "smooth", conf->smooth,
                         "highpass-blur", conf->highpass_blur,
                         "highpass-noise", conf->highpass_noise,
                         "amplify", conf->amplify,
                         "surface-min", conf->surface_min,
                         "surface-max", conf->surface_max,
                         "distance-max", conf->distance_max,
                         NULL);

  port = g_strdup_printf("%d", portno);

  /* we use tuio protocol for calibration and uinput in real usage */
  g_object_set(blobtuio,
#ifdef G_OS_WIN32
#else
      "uinput", FALSE,
#endif
      "matrix", conf->matrix,
      "address", "127.0.0.1",
      "port", port,
      "tuio", TRUE,
      NULL);

  g_free(port);
      
  gst_bin_add_many (GST_BIN (pipeline), src, 
      ffmpegcolorspace, blobtuio, NULL);

  gst_element_link(src, ffmpegcolorspace);

  /* convert to grayscale and fix width and height */
  caps = gst_caps_new_simple ("video/x-raw-gray",
      "bpp", G_TYPE_INT, 8,
      "width", G_TYPE_INT, conf->camera_width,
      "height", G_TYPE_INT, conf->camera_height,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      NULL);

//  gst_element_link_filtered(src, ffmpegcolorspace, caps);
  gst_element_link_filtered(ffmpegcolorspace, blobtuio, caps);
  gst_caps_unref(caps);
//  gst_element_link_many(ffmpegcolorspace, blobtuio, NULL);

  /* you would normally check that the elements were created properly */
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_watch (bus, cb_bus, NULL);

  if (gst_element_set_state (pipeline, GST_STATE_PLAYING) != GST_STATE_CHANGE_SUCCESS) {
    prompt_error_and_exit("Cannot start pipeline\n");
  }
}

