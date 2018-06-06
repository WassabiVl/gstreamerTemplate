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
#include <gdk/gdkx.h>
#include <getopt.h>
#include <string.h>
#include <gst/interfaces/xoverlay.h>
#include <gst/gst.h>
#include <lo/lo.h>
#include <pthread.h>

#undef DEBUG

#define WID(s) GTK_WIDGET (gtk_builder_get_object (gtkbuilder, s))
#define AID(s) GTK_ADJUSTMENT (gtk_builder_get_object (gtkbuilder, s))

typedef struct _blob blob;
struct _blob {
  gint id;
  gfloat x;
  gfloat y;
  gfloat X;
  gfloat Y;
  gfloat m;
};

#define DISPLAY_BLOB_SIZE 10
static int camera_width = 320;
static int camera_height = 240;

#define MAX_NUM_OF_BLOB 16

/* maintain a flipflop blob_list, one for updating, one for drawing */
static blob blob_list[2][MAX_NUM_OF_BLOB];
static gint blob_updating_index;
static gint frameseq;
static gint no_of_blobs;
static pthread_mutex_t blob_mutex = PTHREAD_MUTEX_INITIALIZER;

static GtkWidget *source_video, *background_video, *smooth_video,
    *highpass_video, *amplify_video;
static  GtkWidget *threshold_video;

static GtkAdjustment *threshold_adjustment, *min_blob_size_adjustment;
static GtkAdjustment *max_blob_size_adjustment, *max_blob_dist_adjustment; 
static GtkAdjustment *smooth_adjustment;
static GtkAdjustment *highpass_blur_adjustment, *highpass_noise_adjustment;
static GtkAdjustment *amplify_adjustment;
static GtkCheckButton *trackdark_checkbutton;

static GstElement *pipeline;
static GstElement *blobtuio;
static guint source_video_embed_xid;
static guint bg_video_embed_xid;
static guint smooth_video_embed_xid;
static guint highpass_video_embed_xid;
static guint amplify_video_embed_xid;
static guint threshold_video_embed_xid;

static void
errorlo(int num, const char *msg, const char *path)
{
  g_warning("liblo server error %d in path %s: %s\n", num, path, msg);
}

static gboolean 
tracker_update(gpointer data) 
{
// TEMP FIXME
//  gtk_widget_queue_draw(threshold_video);
  return FALSE;
}

static int
tuio2Dcur_handler(const char *path, const char *types, lo_arg **argv,
                    int argc, void *data, void *user)
{
  static gint no_of_alive_blobs;
  static gint cur_blob_received;

  pthread_mutex_lock(&blob_mutex);

  if (!strcmp((char *) argv[0],"set") && (argc > 5)) {
#ifdef DEBUG
    g_message("ID: %d x: %f y: %f X: %f Y: %f m: %f\n", argv[1]->i, argv[2]->f,
        argv[3]->f, argv[4]->f, argv[5]->f, argv[6]->f);
#endif
    if ((cur_blob_received < no_of_alive_blobs) && (cur_blob_received < MAX_NUM_OF_BLOB)) {
      blob_list[blob_updating_index][cur_blob_received].id = argv[1]->i;
      blob_list[blob_updating_index][cur_blob_received].x = argv[2]->f;
      blob_list[blob_updating_index][cur_blob_received].y = argv[3]->f;
      blob_list[blob_updating_index][cur_blob_received].X = argv[4]->f;
      blob_list[blob_updating_index][cur_blob_received].Y = argv[5]->f;
      blob_list[blob_updating_index][cur_blob_received].m = argv[6]->f;
    }
    cur_blob_received++;
    if ((cur_blob_received == no_of_alive_blobs) || (cur_blob_received == MAX_NUM_OF_BLOB)) {
      no_of_blobs = no_of_alive_blobs;
      if (no_of_blobs > MAX_NUM_OF_BLOB)
        no_of_blobs = MAX_NUM_OF_BLOB;
      blob_updating_index = 1 - blob_updating_index;   
      g_idle_add(tracker_update, NULL);
    }
  } else if (!strcmp((char *) argv[0],"fseq") && argv[1]->i != -1 ) {
#ifdef DEBUG
    g_message("frameseq: %d\n", argv[1]->i);
#endif
    /* notify to update ui */
    frameseq = argv[1]->i;
  } else if (!strcmp((char *) argv[0],"alive") ) {
    cur_blob_received = 0;
    no_of_alive_blobs = argc-1;
    if (no_of_blobs > MAX_NUM_OF_BLOB)
      no_of_blobs = MAX_NUM_OF_BLOB;
#ifdef DEBUG
    {
    int j;
    for (j = 1; j < argc; j++) {
      g_message("Id[%d] - %d ",j-1, argv[j]->i);
    }
    g_message("\n");
    }
#endif
    if (no_of_alive_blobs == 0) {
      no_of_blobs = 0;
      g_idle_add(tracker_update, NULL);
    }
  }
  pthread_mutex_unlock(&blob_mutex);

  return 0;
}

static lo_server_thread
initlo(const char *port)
{
  lo_server_thread st;
  st = lo_server_thread_new(port, errorlo);
  lo_server_thread_add_method(st, "/tuio/2Dcur", NULL, tuio2Dcur_handler, NULL);
  lo_server_thread_start(st);
  return st;
}

#if 0
static gboolean
tracked_video_expose_event(GtkWidget *widget, GdkEventExpose *event, gpointer data)
{
  int width, height;
  int x, y;
  int i;

  gdk_drawable_get_size(widget->window, &width, &height);

  /* clean the background to black */
  gdk_draw_rectangle (widget->window, widget->style->black_gc,
      TRUE, 0, 0, width, height);

  pthread_mutex_lock(&blob_mutex);

  /* go through all blobs and draw it */
  for (i=0; i<no_of_blobs; i++) {
    x = blob_list[1-blob_updating_index][i].x * width / camera_width;
    y = blob_list[1-blob_updating_index][i].y * height / camera_height;
    gdk_draw_arc(widget->window, widget->style->white_gc, TRUE,
        x - DISPLAY_BLOB_SIZE, y - DISPLAY_BLOB_SIZE,
        DISPLAY_BLOB_SIZE, DISPLAY_BLOB_SIZE, 0, 360*64);

    /* draw the id and coordinate under the white dot */
    /* blob_list[1-blob_updating_index][i].id; */
  }

  pthread_mutex_unlock(&blob_mutex);
  
#if 0
  PangoLayout *copyright_layout, *version_layout;
  PangoRectangle link, logical;
  gchar *version;

  /* Draw background image */
  gdk_draw_pixbuf(widget->window,
      widget->style->bg_gc[GTK_WIDGET_STATE(widget)],
      image,
      0, 0,
      0, 0,
      -1,-1, 
      GDK_RGB_DITHER_NORMAL,
      0, 0);
  /* Draw logo at top right */
  gdk_draw_pixbuf(widget->window,
      widget->style->fg_gc[GTK_WIDGET_STATE(widget)],
      logo,
      0, 0,
      bg_width - logo_width, 0,
      -1,-1, 
      GDK_RGB_DITHER_NORMAL,
      0, 0);
  /* Draw version under the logo */
  version = g_strdup_printf ("version %s", VERSION);
  version_layout = gtk_widget_create_pango_layout (widget, 
      version);
  pango_layout_set_alignment(version_layout, PANGO_ALIGN_RIGHT);
  pango_layout_get_pixel_extents(version_layout, &link, &logical);
  gdk_draw_layout(widget->window,
      widget->style->fg_gc[GTK_WIDGET_STATE(widget)],
      bg_width - logical.width, logo_height,
      version_layout);
  g_free (version);
  g_object_unref(version_layout);

  /* Draw copyright at bottom right */
  copyright_layout = gtk_widget_create_pango_layout (widget, 
      _(copyright));
  pango_layout_set_alignment(copyright_layout, PANGO_ALIGN_RIGHT);
  pango_layout_set_width(copyright_layout, -1);
  pango_layout_get_pixel_extents(copyright_layout, &link, &logical);
  gdk_draw_layout(widget->window,
      widget->style->fg_gc[GTK_WIDGET_STATE(widget)],
      bg_width - logical.width, bg_height - logical.height,
      copyright_layout);
  g_object_unref(copyright_layout);
#endif
  return TRUE;
}
#endif
static void
cb_learn_bg(GtkButton *button, gpointer user_data)
{
  g_object_set(G_OBJECT(blobtuio), "learn_background_counter", 30, NULL);
}

static GtkBuilder *
create_window(void)
{
  GtkBuilder   *dialog;
  GError       *error = NULL;
#ifdef G_OS_WIN32
  gchar *dir, *uifile;
#endif

  dialog = gtk_builder_new ();
#ifdef G_OS_WIN32
  dir = g_win32_get_package_installation_directory_of_module (NULL);
  uifile = g_build_filename (dir, "share", "gst-webcam-input",
      "gst-webcam-input.ui", NULL);
  g_free (dir);

  gtk_builder_add_from_file (dialog, uifile, &error);

  g_free (uifile);
#else
  gtk_builder_add_from_file (dialog, WEBCAM_DATADIR G_DIR_SEPARATOR_S "gst-webcam-input.ui", &error);
#endif
  if (error != NULL) {
    g_error("Error loading UI file: %s", error->message);
    return NULL;
  }

  return dialog;
}

static void
cb_blobtuio_adjustment( GtkAdjustment *adj, const char *blobprop_name)
{
  g_object_set(blobtuio, blobprop_name, (gint) adj->value, NULL);
}

static void
cb_blobtuio_toggle( GtkToggleButton *btn, const char *blobprop_name)
{
  gboolean b;
  b = gtk_toggle_button_get_active (btn);
  g_object_set(blobtuio, blobprop_name, b, NULL);
}

static void
cb_new_pad (GstElement* decodebin, GstPad* pad, gboolean last, GstElement *next)
{
    GstPad* next_pad = gst_element_get_static_pad (next, "sink");

    /* only link once */
    if (GST_PAD_IS_LINKED(next_pad)) {
        g_object_unref(next_pad);
        return;
    }

    GstCaps* caps = gst_pad_get_caps(pad);
    GstStructure* str = gst_caps_get_structure(caps, 0);
    if (!g_strrstr (gst_structure_get_name (str), "video")) {
        gst_caps_unref(caps);
        gst_object_unref(next_pad);
        return;
    }
    gst_caps_unref (caps);

    GstPadLinkReturn ret = gst_pad_link (pad, next_pad);
    if (ret != GST_PAD_LINK_OK)
        g_warning ("Failed to link with decodebin!\n");
}

static GstBusSyncReply
bus_sync_handler (GstBus * bus, GstMessage * message, gpointer *unused)
{
  if ((GST_MESSAGE_TYPE (message) == GST_MESSAGE_ELEMENT) &&
      gst_structure_has_name (message->structure, "prepare-xwindow-id")) {
    GstElement *element = GST_ELEMENT (GST_MESSAGE_SRC (message));
    const gchar *name = gst_element_get_name (element);
    guint embed_xid = 0;


    /* The gst example get GDK_WINDOW_XID here, but it is not thread safe
     * https://bugzilla.gnome.org/show_bug.cgi?id=599885
     */

    if (g_object_class_find_property (G_OBJECT_GET_CLASS (element),
            "force-aspect-ratio")) {
      g_object_set (element, "force-aspect-ratio", TRUE, NULL);
    }

    if (strcmp(name, "srcimagesink") == 0) {
      embed_xid = source_video_embed_xid;
    } else if (strcmp(name, "bgimagesink") == 0) {
      embed_xid = bg_video_embed_xid;
    } else if (strcmp(name, "smoothimagesink") == 0) {
      embed_xid = smooth_video_embed_xid;
    } else if (strcmp(name, "highpassimagesink") == 0) {
      embed_xid = highpass_video_embed_xid;
    } else if (strcmp(name, "amplifyimagesink") == 0) {
      embed_xid = amplify_video_embed_xid;
    } else if (strcmp(name, "thresholdimagesink") == 0) {
      embed_xid = threshold_video_embed_xid;
    }
    if (embed_xid != 0)
      gst_x_overlay_set_xwindow_id (GST_X_OVERLAY (GST_MESSAGE_SRC (message)),
          embed_xid);
  }
  return GST_BUS_PASS;
}

static void
connect_bus_signals (GstElement * pipeline)
{
  GstBus *bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));

  /* handle prepare-xwindow-id element message synchronously */
  gst_bus_set_sync_handler (bus, (GstBusSyncHandler) bus_sync_handler,
      NULL);

#if 0
  gst_bus_add_signal_watch_full (bus, G_PRIORITY_HIGH);

  g_signal_connect (bus, "message::state-changed",
      (GCallback) msg_state_changed, pipeline);
  g_signal_connect (bus, "message::segment-done", (GCallback) msg_segment_done,
      pipeline);
  g_signal_connect (bus, "message::async-done", (GCallback) msg_async_done,
      pipeline);

  g_signal_connect (bus, "message::new-clock", (GCallback) message_received,
      pipeline);
  g_signal_connect (bus, "message::error", (GCallback) message_received,
      pipeline);
  g_signal_connect (bus, "message::warning", (GCallback) message_received,
      pipeline);
  g_signal_connect (bus, "message::eos", (GCallback) msg_eos, pipeline);
  g_signal_connect (bus, "message::tag", (GCallback) message_received,
      pipeline);
  g_signal_connect (bus, "message::element", (GCallback) message_received,
      pipeline);
  g_signal_connect (bus, "message::segment-done", (GCallback) message_received,
      pipeline);
  g_signal_connect (bus, "message::buffering", (GCallback) msg_buffering,
      pipeline);
#endif

  gst_object_unref (bus);
}

static void
initgst(gboolean from_video)
{
  GstElement *src, *tee;
  GstElement *decodebin = NULL;
  GstElement *ffmpegcolorspace[7];
  GstElement *src_xvimagesink, *bg_xvimagesink, *smooth_xvimagesink, *highpass_xvimagesink;
  GstElement *amplify_xvimagesink, *threshold_xvimagesink;
  GstElement *queuesrc, *queuebg, *queueblob, *queuesmooth, *queuehighpass, *queueamplify;
  GstElement *queuethreshold;
  GstCaps *caps;
  GstPad *pad, *rpad;
  guint value_uint;
  gboolean value_b;

  pipeline = gst_pipeline_new ("app");

  if (from_video)
    src = gst_element_factory_make ("v4l2src", "source");
  else {
    src = gst_element_factory_make ("filesrc", "source");
    g_object_set (G_OBJECT (src), "location", "FrontDI.m4v", NULL); /* FIXME browse file */
    decodebin = gst_element_factory_make ("decodebin", "decodebin");
  }
  ffmpegcolorspace[0] = gst_element_factory_make ("ffmpegcolorspace", "colorconvert1");
  ffmpegcolorspace[1] = gst_element_factory_make ("ffmpegcolorspace", "colorconvert2");
  ffmpegcolorspace[2] = gst_element_factory_make ("ffmpegcolorspace", "colorconvert3");
  ffmpegcolorspace[3] = gst_element_factory_make ("ffmpegcolorspace", "colorconvert4");
  ffmpegcolorspace[4] = gst_element_factory_make ("ffmpegcolorspace", "colorconvert5");
  ffmpegcolorspace[5] = gst_element_factory_make ("ffmpegcolorspace", "colorconvert6");
  ffmpegcolorspace[6] = gst_element_factory_make ("ffmpegcolorspace", "colorconvert7");

  tee = gst_element_factory_make ("tee", "tee");

  src_xvimagesink = gst_element_factory_make ("xvimagesink", "srcimagesink");
  bg_xvimagesink = gst_element_factory_make ("xvimagesink", "bgimagesink");
  smooth_xvimagesink = gst_element_factory_make ("xvimagesink", "smoothimagesink");
  highpass_xvimagesink = gst_element_factory_make ("xvimagesink", "highpassimagesink");
  amplify_xvimagesink = gst_element_factory_make ("xvimagesink", "amplifyimagesink");
  threshold_xvimagesink = gst_element_factory_make ("xvimagesink", "thresholdimagesink");

  blobtuio = gst_element_factory_make ("blobstotuio", "blobtuio");
  queuesrc = gst_element_factory_make ("queue", "queuesrc");

  queuebg = gst_element_factory_make ("queue", "queuebg");
  queueblob = gst_element_factory_make ("queue", "queueblob");
  queuesmooth = gst_element_factory_make ("queue", "queuesmooth");
  queuehighpass = gst_element_factory_make ("queue", "queuehighpass");
  queueamplify = gst_element_factory_make ("queue", "queueamplify");
  queuethreshold = gst_element_factory_make ("queue", "queuethreshold");

  if (!pipeline || !src || !ffmpegcolorspace[0] || !ffmpegcolorspace[1] ||
      !ffmpegcolorspace[2] || !ffmpegcolorspace[3] || !ffmpegcolorspace[4] ||
      !ffmpegcolorspace[5] || !ffmpegcolorspace[6] || !tee || !src_xvimagesink || !blobtuio ||
      !queuesrc || !queuebg || !queueblob || !queuesmooth || !queuehighpass ||
      !queueamplify || !queuethreshold) {
    g_error("missing element\n");
    return;
  }

  g_object_set(blobtuio, "learn-background-counter", 15, "threshold", 25,
      "smooth", 2, "highpass-blur", 18, "highpass-noise", 4, "amplify", 16,
      "surface-min", 50, "surface-max", 500, "distance-max", 40, NULL);


#if 0 
  gst_bin_add_many (GST_BIN (pipeline), src, 
      ffmpegcolorspace[0], ffmpegcolorspace[1], ffmpegcolorspace[2], ffmpegcolorspace[3],
      ffmpegcolorspace[4], ffmpegcolorspace[5], tee, src_xvimagesink, bg_xvimagesink,
      smooth_xvimagesink, highpass_xvimagesink, amplify_xvimagesink, 
      blobtuio, queuesrc, queuebg, queueblob, queuesmooth, queuehighpass, queueamplify,
      NULL);
#else
  gst_bin_add_many (GST_BIN (pipeline), src, 
      ffmpegcolorspace[0], ffmpegcolorspace[1], ffmpegcolorspace[2], ffmpegcolorspace[3],
      ffmpegcolorspace[4], ffmpegcolorspace[5], ffmpegcolorspace[6], tee, src_xvimagesink, bg_xvimagesink,
      smooth_xvimagesink, highpass_xvimagesink, amplify_xvimagesink, threshold_xvimagesink,
      blobtuio, queuesrc, queuebg, queueblob, queuesmooth, queuehighpass, queueamplify, queuethreshold,
      NULL);
  if (!from_video)
    gst_bin_add (GST_BIN (pipeline), decodebin);
#endif
  if (from_video)
    gst_element_link(src, ffmpegcolorspace[0]);
  else {
    gst_element_link_pads (src, "src", decodebin, "sink");
    g_signal_connect (decodebin, "new-decoded-pad", G_CALLBACK (cb_new_pad), ffmpegcolorspace[0]);
  }

  /* convert to grayscale and fix width and height */
  caps = gst_caps_new_simple ("video/x-raw-gray",
  	      "bpp", G_TYPE_INT, 8,
	      "width", G_TYPE_INT, camera_width,
	      "height", G_TYPE_INT, camera_height,
	      NULL);

  gst_element_link_filtered(ffmpegcolorspace[0], tee, caps);
  gst_caps_unref (caps);
  gst_element_link_many(tee, queuesrc, ffmpegcolorspace[1], src_xvimagesink, NULL);
  gst_element_link_many(tee, queueblob, blobtuio, NULL);

  pad = gst_element_get_static_pad (queuebg, "sink");
  rpad = gst_element_get_request_pad (blobtuio, "srcbg");
  gst_pad_link (rpad, pad);
  gst_object_unref (rpad);
  gst_object_unref (pad);
  gst_element_link_many(queuebg, ffmpegcolorspace[2], bg_xvimagesink, NULL);

  pad = gst_element_get_static_pad (queuesmooth, "sink");
  rpad = gst_element_get_request_pad (blobtuio, "srcsmooth");
  gst_pad_link (rpad, pad);
  gst_object_unref (rpad);
  gst_object_unref (pad);
  gst_element_link_many(queuesmooth, ffmpegcolorspace[3], smooth_xvimagesink, NULL);

  pad = gst_element_get_static_pad (queuehighpass, "sink");
  rpad = gst_element_get_request_pad (blobtuio, "srchighpass");
  gst_pad_link (rpad, pad);
  gst_object_unref (rpad);
  gst_object_unref (pad);
  gst_element_link_many(queuehighpass, ffmpegcolorspace[4], highpass_xvimagesink, NULL);

  pad = gst_element_get_static_pad (queueamplify, "sink");
  rpad = gst_element_get_request_pad (blobtuio, "srcamplify");
  gst_pad_link (rpad, pad);
  gst_object_unref (rpad);
  gst_object_unref (pad);
  gst_element_link_many(queueamplify, ffmpegcolorspace[5], amplify_xvimagesink, NULL);

  pad = gst_element_get_static_pad (queuethreshold, "sink");
  rpad = gst_element_get_request_pad (blobtuio, "srcthreshold");
  gst_pad_link (rpad, pad);
  gst_object_unref (rpad);
  gst_object_unref (pad);
  gst_element_link_many(queuethreshold, ffmpegcolorspace[6], threshold_xvimagesink, NULL);

  /* set the ui the default blobtuio parameters */
  g_object_get (blobtuio, "smooth", &value_uint, NULL);
  gtk_adjustment_set_value (smooth_adjustment, (gdouble)value_uint);
  g_object_get (blobtuio, "highpass-blur", &value_uint, NULL);
  gtk_adjustment_set_value (highpass_blur_adjustment, (gdouble)value_uint);
  g_object_get (blobtuio, "highpass-noise", &value_uint, NULL);
  gtk_adjustment_set_value (highpass_noise_adjustment, (gdouble)value_uint);
  g_object_get (blobtuio, "amplify", &value_uint, NULL);
  gtk_adjustment_set_value (amplify_adjustment, (gdouble)value_uint);
  g_object_get (blobtuio, "threshold", &value_uint, NULL);
  gtk_adjustment_set_value (threshold_adjustment, (gdouble)value_uint);
  g_object_get (blobtuio, "surface-min", &value_uint, NULL);
  gtk_adjustment_set_value (min_blob_size_adjustment, (gdouble)value_uint);
  g_object_get (blobtuio, "surface-max", &value_uint, NULL);
  gtk_adjustment_set_value (max_blob_size_adjustment, (gdouble)value_uint);
  g_object_get (blobtuio, "distance-max", &value_uint, NULL);
  gtk_adjustment_set_value (max_blob_dist_adjustment, (gdouble)value_uint);
  g_object_get (blobtuio, "trackdark", &value_b, NULL);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(trackdark_checkbutton), value_b);

  connect_bus_signals (pipeline);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
}

static void
v4l2_radio_button_release_event(GtkWidget   *widget,
    GdkEventButton *event)
{
  gboolean b;
  b = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));

  if (b)
    return;
  
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), TRUE);

  if (pipeline) {
    gst_element_set_state (pipeline, GST_STATE_NULL);
    g_object_unref(pipeline);
    pipeline = NULL;
  }
  initgst(TRUE);
}

static void
file_radio_button_release_event(GtkWidget   *widget,
    GdkEventButton *event)
{
  gboolean b;
  b = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));

  if (b)
    return;

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), TRUE);
  
  if (pipeline) {
    gst_element_set_state (pipeline, GST_STATE_NULL);
    g_object_unref(pipeline);
    pipeline = NULL;
  }
  initgst(FALSE);
}

static GtkBuilder *
initgtklayout()
{
  GtkBuilder *gtkbuilder;
  GtkWidget *main_window;
  GtkWidget *learn_bg_button;

  gtkbuilder = create_window();

  main_window = WID("main_window");
  source_video = WID("source_video");
  background_video = WID("background_video");
  smooth_video = WID("smooth_video");
  highpass_video = WID("highpass_video");
  amplify_video = WID("amplify_video");
  threshold_video = WID("tracked_video");

  g_signal_connect (WID ("v4l2_radio"), "button_release_event",
      G_CALLBACK (v4l2_radio_button_release_event), NULL);
  g_signal_connect (WID ("file_radio"), "button_release_event",
      G_CALLBACK (file_radio_button_release_event), NULL);

  threshold_adjustment = AID("threshold_adjustment");
  min_blob_size_adjustment = AID("min_blob_size_adjustment");
  max_blob_size_adjustment = AID("max_blob_size_adjustment");
  max_blob_dist_adjustment = AID("max_blob_dist_adjustment");
  smooth_adjustment = AID("smooth_adjustment");
  highpass_blur_adjustment = AID("highpass_blur_adjustment");
  highpass_noise_adjustment = AID("highpass_noise_adjustment");
  amplify_adjustment = AID("amplify_adjustment");
  trackdark_checkbutton = GTK_CHECK_BUTTON(gtk_builder_get_object(gtkbuilder, "trackdark_enable"));

  gtk_signal_connect (GTK_OBJECT (threshold_adjustment), "value_changed",
      GTK_SIGNAL_FUNC (cb_blobtuio_adjustment), "threshold");
  gtk_signal_connect (GTK_OBJECT (min_blob_size_adjustment), "value_changed",
      GTK_SIGNAL_FUNC (cb_blobtuio_adjustment), "surface-min");
  gtk_signal_connect (GTK_OBJECT (max_blob_size_adjustment), "value_changed",
      GTK_SIGNAL_FUNC (cb_blobtuio_adjustment), "surface-max");
  gtk_signal_connect (GTK_OBJECT (max_blob_dist_adjustment), "value_changed",
      GTK_SIGNAL_FUNC (cb_blobtuio_adjustment), "distance-max");
  gtk_signal_connect (GTK_OBJECT (smooth_adjustment), "value_changed",
      GTK_SIGNAL_FUNC (cb_blobtuio_adjustment), "smooth");
  gtk_signal_connect (GTK_OBJECT (highpass_blur_adjustment), "value_changed",
      GTK_SIGNAL_FUNC (cb_blobtuio_adjustment), "highpass-blur");
  gtk_signal_connect (GTK_OBJECT (highpass_noise_adjustment), "value_changed",
      GTK_SIGNAL_FUNC (cb_blobtuio_adjustment), "highpass-noise");
  gtk_signal_connect (GTK_OBJECT (amplify_adjustment), "value_changed",
      GTK_SIGNAL_FUNC (cb_blobtuio_adjustment), "amplify");
  gtk_signal_connect (GTK_OBJECT (trackdark_checkbutton), "toggled",
      GTK_SIGNAL_FUNC (cb_blobtuio_toggle), "trackdark");

  learn_bg_button = WID("learn_background_button");

  g_signal_connect (G_OBJECT (learn_bg_button), "clicked", G_CALLBACK (cb_learn_bg), NULL);
  // TODO
//  g_signal_connect(tracked_video, "expose_event",
//      G_CALLBACK(tracked_video_expose_event), NULL);

  /* show the gui. */
  gtk_widget_show (main_window);

  g_signal_connect (main_window, "destroy",
      G_CALLBACK (gtk_main_quit), NULL);

  /* to ensure gst playback window is ready before start the gst pipeline */
  gtk_widget_realize(main_window);

  source_video_embed_xid = GDK_WINDOW_XWINDOW(source_video->window);
  bg_video_embed_xid = GDK_WINDOW_XWINDOW(background_video->window);
  smooth_video_embed_xid = GDK_WINDOW_XWINDOW(smooth_video->window);
  highpass_video_embed_xid = GDK_WINDOW_XWINDOW(highpass_video->window);
  amplify_video_embed_xid = GDK_WINDOW_XWINDOW(amplify_video->window);
  threshold_video_embed_xid = GDK_WINDOW_XWINDOW(threshold_video->window);

  return gtkbuilder;
}

const struct option long_opt[] =
{
  {"camera_resolution", 1, NULL, 'c' },
  {NULL, 0, NULL, '\0' },
};

static void
parse_args (int argc, char *argv[])
{
  int i;
  while (1) {
    i = getopt_long(argc, argv, "c:", long_opt, NULL);
    if (i == -1)
      break;
    switch (i) {
      default:
        break;
      case 'c':
        if (sscanf(optarg, "%dx%d", &camera_width, &camera_height) != 2) {
          g_error("camera resolution invalid\n");
        }
        if ((camera_width <= 0) || (camera_height <= 0)) {
          g_error("camera resolution invalid\n");
        }
        break;
    }
  }
}

int
main (int   argc,
      char *argv[])
{
  GtkBuilder *gtkbuilder;

  if (!g_thread_supported())
    g_thread_init(NULL);

  gst_init(&argc, &argv);
  gtk_init(&argc, &argv);

  parse_args (argc, argv);

  /* init osc */
  initlo("3333");

  /* init gtk layout */
  gtkbuilder = initgtklayout();

  initgst(TRUE);

  gtk_main();

  gst_object_unref(gtkbuilder);
  if (pipeline)
    gst_object_unref(GST_OBJECT (pipeline));
  return 0;
}

