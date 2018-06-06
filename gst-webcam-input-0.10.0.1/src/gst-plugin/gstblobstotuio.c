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

#include <gst/gst.h>
#include <lo/lo.h>
#include <string.h>
#include <stdio.h>

#include <fcntl.h>      
#if !defined(G_OS_WIN32)
#include <linux/input.h>
#include <linux/uinput.h>
#endif
#include <sys/time.h>   
#include <unistd.h>

#include "gstblobstotuio.h"
#include "blob_detector.h"
#include "image_utils.h"

GST_DEBUG_CATEGORY_STATIC (gst_blobs_to_tuio_debug);

#define GST_CAT_DEFAULT gst_blobs_to_tuio_debug

#define GST_BLOBSTOTUIO_GET_PRIVATE(obj)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_BLOBSTOTUIO, \
   GstBlobsToTUIOPrivate))

typedef struct _Blob                Blob;
typedef struct _BlobList            BlobList;

/* hal for xserver-xorg don't like ABS_PRESSURE,
 * otherwise it think it is a synaptics driver */
#undef USE_ABS_PRESSURE

/* quick check if linux kernel > 2.6.29 for multitouch input event support */
#if defined(ABS_MT_TOUCH_MAJOR)
#define USE_MT_EVENT
#endif

#undef DEBUG

struct _Blob
{
  gint id;
  gfloat x;
  gfloat y;
  gfloat major;
};

enum {
  BG_SRC_PADi = 0,
  SMOOTH_SRC_PAD,
  HIGHPASS_SRC_PAD,
  AMPLIFY_SRC_PAD,
  THRESHOLD_SRC_PAD,
  MAX_SRC_PAD,
};

struct _GstBlobsToTUIOPrivate
{
  GstPad *processing_srcpad[MAX_SRC_PAD];
  GstPad *sinkpad;

  gint width;
  gint height;
  gint *markbuf;
  guint8* background_buf; /* learnt background buffer */
  guint16* background_buf_fractional; /* fixed floating point (.16) */
  guint background_buf_learning_init_counter;
  guint8 *working_buf1;
  guint8 *working_buf2;
  guint8 *image_blur_temp;
  
  GSList *blobs;
  gint num_of_frame;
  
  /* matrix to transform from camera coordinate */
  /* to 0-1,0-1 */
  gfloat matrix[6];
  
  gboolean trackdark;
  guint smooth;
  guint highpass_blur;
  guint highpass_noise;
  guint amplify_shift;
  guint8 threshold;

#if !defined(G_OS_WIN32)
  /* Linux kernel userspace input driver parameters */
  gboolean uinput;
  gchar *uinput_devname;
#if defined(USE_MT_EVENT)
  gboolean uinput_mt; /* using Linux device multitouch protocol */
#endif
  gboolean uinput_up; /* internal flag to check touch up event sent or not */
  guint uinput_maxx; /* abs param report to evdev, and evdev will scale it */
  guint uinput_minx;
  guint uinput_maxy;
  guint uinput_miny;
  int ufile;
#endif

  /* tuio parameters */
  gboolean tuio;
  lo_address loaddress;
  gchar *address;
  gchar *port;
  
  guint surface_min;
  guint surface_max;
  guint distance_max;
};

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_MATRIX,
  PROP_TRACK_DARK,
  PROP_SMOOTH,
  PROP_HIGHPASS_BLUR,
  PROP_HIGHPASS_NOISE,
  PROP_AMPLIFY,
  PROP_THRESHOLD,
  PROP_LEARN_BACKGROUND_COUNTER,
  PROP_UINPUT,
  PROP_UINPUT_DEVNAME,
#if defined(USE_MT_EVENT)
  PROP_UINPUT_MT,
#endif
  PROP_UINPUT_ABS_X_RANGE,
  PROP_UINPUT_ABS_Y_RANGE,
  PROP_TUIO,
  PROP_ADDRESS,
  PROP_PORT,
  PROP_SURFACEMIN,
  PROP_SURFACEMAX,
  PROP_DISTANCEMAX
};

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-gray,bpp=8,depth=8")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src%s",
    GST_PAD_SRC,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("video/x-raw-gray,bpp=8,depth=8")
    );

GST_BOILERPLATE (GstBlobsToTUIO, gst_blobs_to_tuio, GstElement,
    GST_TYPE_ELEMENT);

static void gst_blobs_to_tuio_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_blobs_to_tuio_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstPad *gst_blobs_to_tuio_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * unused);
static void gst_blobs_to_tuio_release_pad (GstElement * element,
    GstPad * pad);
static void gst_blobs_to_tuio_finalize (GstBlobsToTUIO * filter);
static GstFlowReturn gst_blobs_to_tuio_chain(GstPad * pad, GstBuffer * buf);

static gboolean gst_blobs_to_tuio_set_caps (GstPad * pad, GstCaps * caps);

static void
convert_coord(GstBlobsToTUIOPrivate * priv, gfloat xsrc, gfloat ysrc,
    gfloat *xdst, gfloat *ydst)
{
  *xdst = priv->matrix[0] * xsrc + priv->matrix[1] * ysrc + priv->matrix[2];
  *ydst = priv->matrix[3] * xsrc + priv->matrix[4] * ysrc + priv->matrix[5];
}

static void
blob_list_update(GstBlobsToTUIOPrivate * priv, GArray *zones)
{
  static gint next_blob_id = 0;
  Zone *z;
  Blob *b;
  GSList *node, *nextnode;
  gint i;
  gint d;

  ++priv->num_of_frame;

  /* go through all the blob, found the nearest zone, if not found delete it */
  /* any good speed up algorithm ?! */
  for (node = priv->blobs; node; node = nextnode) {
    Zone *zone = NULL;
    gint dist = G_MAXINT;

    nextnode = g_slist_next(node);
    b = (Blob *)node->data;

    for (i = 0; i < zones->len; i++) {
      gint zx, zy;
      z = &g_array_index(zones, Zone, i);
      zx = z->total_x / z->surface_size;
      zy = z->total_y / z->surface_size;

      d = (b->x - zx)*(b->x - zx) + (b->y - zy)*(b->y - zy);
      if (d > (priv->distance_max*priv->distance_max))
        continue;
      /* REVISIT do we need to match the size also for a better match ? */
      if (d < dist) {
        dist = d;
        zone = z;
      }
    }
    /* we need to mark that zone as used to prevent it match to other blob */
    if ((zone != NULL) && !zone->matched) {
      /* update blob information */
      b->x = zone->total_x / zone->surface_size;
      b->y = zone->total_y / zone->surface_size;
      b->major = z->surface_size;
      zone->matched = TRUE;
    } else {
      /* remove old unmatched blob */
      /* TODO: signal that the Blob was removed */
      g_free(b);
      priv->blobs = g_slist_delete_link(priv->blobs, node);
    }
  }

  /* add new blob */
  for (i = 0; i<zones->len; i++) {
    z = &g_array_index(zones, Zone, i);
    /* already matched toa previous blob */
    if (z->matched)
      continue;

    /* TODO: signal that a new Blob was added */
    b = g_new(Blob, 1);
    b->x = z->total_x / z->surface_size;
    b->y = z->total_y / z->surface_size;
    b->major = z->surface_size;
    b->id = next_blob_id++;
    priv->blobs = g_slist_append(priv->blobs, b);
  }
}

#if !defined(G_OS_WIN32)
static void
send_uinput (GstBlobsToTUIOPrivate *priv)
{
  GSList *blobCursor;
  Blob *blob;
  struct input_event event;
  gfloat x, y;
  int ret;

  if (priv->ufile < 0)
    return;

  /* we only send the first blob in the list for single touch input event */
  blobCursor = priv->blobs;

  if (blobCursor == NULL) {
    if (!priv->uinput_up) {
      gettimeofday(&event.time, NULL);
      event.type = EV_KEY;
      event.code = BTN_MOUSE;
      event.value = 0;
      ret = write(priv->ufile, &event, sizeof(event));

#if defined(USE_ABS_PRESSURE)
      gettimeofday(&event.time, NULL);
      event.type = EV_ABS;
      event.code = ABS_PRESSURE;
      event.value = 0;
      ret = write(priv->ufile, &event, sizeof(event));
#endif
      gettimeofday(&event.time, NULL);
      event.type = EV_SYN;
      event.code = SYN_REPORT;
      event.value = 0;
      ret = write(priv->ufile, &event, sizeof(event));
      priv->uinput_up = TRUE;
    }
  } else {
    if (priv->uinput_up == TRUE) {
      gettimeofday(&event.time, NULL);
      event.type = EV_KEY;
      event.code = BTN_MOUSE;
      event.value = 1;
      ret = write(priv->ufile, &event, sizeof(event));
      priv->uinput_up = FALSE;
    }
    blob = (Blob *)(blobCursor->data);
    convert_coord(priv, (float)(blob->x), (float)(blob->y), &x, &y);

    gettimeofday(&event.time, NULL);
    event.type = EV_ABS;
    event.code = ABS_X;
    event.value = x;
    ret = write(priv->ufile, &event, sizeof(event));

    gettimeofday(&event.time, NULL);
    event.type = EV_ABS;
    event.code = ABS_Y;
    event.value = y;
    ret = write(priv->ufile, &event, sizeof(event));

#if defined(USE_ABS_PRESSURE)
    gettimeofday(&event.time, NULL);
    event.type = EV_ABS;
    event.code = ABS_PRESSURE;
    event.value = blob->major;
    ret = write(priv->ufile, &event, sizeof(event));
#endif
    gettimeofday(&event.time, NULL);
    event.type = EV_SYN;
    event.code = SYN_REPORT;
    event.value = 0;
    ret = write(priv->ufile, &event, sizeof(event));
  }
}

#if defined(USE_MT_EVENT)
static void
send_uinput_mt (GstBlobsToTUIOPrivate *priv)
{
  GSList *blobCursor;
  struct input_event event;
  int ret;

  if (priv->ufile < 0)
    return;

  for (blobCursor = priv->blobs; blobCursor != NULL;
      blobCursor = g_slist_next(blobCursor)) {
    Blob *blob = (Blob *)(blobCursor->data);
    gfloat x, y;
    convert_coord(priv, (float)(blob->x), (float)(blob->y), &x, &y);

    gettimeofday(&event.time, NULL);
    event.type = EV_ABS;
    event.code = ABS_MT_TOUCH_MAJOR;
    event.value = blob->major;
    ret = write(priv->ufile, &event, sizeof(event));
            
    gettimeofday(&event.time, NULL);
    event.type = EV_ABS;
    event.code = ABS_MT_POSITION_X;
    event.value = x;
    ret = write(priv->ufile, &event, sizeof(event));
            
    gettimeofday(&event.time, NULL);
    event.type = EV_ABS;
    event.code = ABS_MT_POSITION_Y;
    event.value = y;
    ret = write(priv->ufile, &event, sizeof(event));

    gettimeofday(&event.time, NULL);
    event.type = EV_ABS;
    event.code = ABS_MT_TRACKING_ID;
    event.value = blob->id;
    ret = write(priv->ufile, &event, sizeof(event));

    gettimeofday(&event.time, NULL);
    event.type = EV_SYN;
    event.code = SYN_MT_REPORT;
    event.value = 0;
    ret = write(priv->ufile, &event, sizeof(event));
  }

  if ((priv->blobs == NULL) && (!priv->uinput_up)) {
    /* touch up event !!! */
    gettimeofday(&event.time, NULL);
    event.type = EV_ABS;
    event.code = ABS_MT_TOUCH_MAJOR;
    event.value = 0;
    ret = write(priv->ufile, &event, sizeof(event));
            
    gettimeofday(&event.time, NULL);
    event.type = EV_ABS;
    event.code = ABS_MT_POSITION_X;
    event.value = 0;
    ret = write(priv->ufile, &event, sizeof(event));
            
    gettimeofday(&event.time, NULL);
    event.type = EV_ABS;
    event.code = ABS_MT_POSITION_Y;
    event.value = 0;
    ret = write(priv->ufile, &event, sizeof(event));
            
    gettimeofday(&event.time, NULL);
    event.type = EV_SYN;
    event.code = SYN_MT_REPORT;
    event.value = 0;
    ret = write(priv->ufile, &event, sizeof(event));

    priv->uinput_up = TRUE;

    gettimeofday(&event.time, NULL);
    event.type = EV_SYN;
    event.code = SYN_REPORT;
    event.value = 0;
    ret = write(priv->ufile, &event, sizeof(event));
  } else if (priv->blobs != NULL) {
    priv->uinput_up = FALSE;
    gettimeofday(&event.time, NULL);
    event.type = EV_SYN;
    event.code = SYN_REPORT;
    event.value = 0;
    ret = write(priv->ufile, &event, sizeof(event));
  }
}
#endif
#endif /* !WIN32 */

#define MAX_BUNDLE_SET 16

static void
send_tuio (GstBlobsToTUIOPrivate *priv)
{
  lo_message alivemsg;
  lo_message fseqmsg;
  gint setcount = 0;
  gint i;
  lo_message setmsg[MAX_BUNDLE_SET+1];
  lo_bundle  bundle;
  GSList *blobCursor;

  bundle = lo_bundle_new(LO_TT_IMMEDIATE);
  /* alive message */
  alivemsg = lo_message_new();
  lo_message_add_string(alivemsg, "alive");
  for (blobCursor = priv->blobs; blobCursor != NULL;
    blobCursor = g_slist_next(blobCursor)) {
    Blob *blob = (Blob *)(blobCursor->data);
    lo_message_add_int32(alivemsg, blob->id);
  }
  /* sequence number */
  fseqmsg = lo_message_new();
  lo_message_add_string(fseqmsg, "fseq");
  lo_message_add_int32(fseqmsg, (int)(priv->num_of_frame));

  lo_bundle_add_message(bundle, "/tuio/2Dcur", alivemsg);

  /* send set */
  for (blobCursor = priv->blobs; blobCursor != NULL;
    blobCursor = g_slist_next(blobCursor)) {
    gfloat x, y;
    Blob *blob = (Blob *)(blobCursor->data);
    convert_coord(priv, (float)(blob->x), (float)(blob->y), &x, &y);
    GST_DEBUG_OBJECT(priv, "blob id=%d, x=%f, y=%f\n", blob->id, x, y);
    setmsg[setcount] = lo_message_new();
    lo_message_add_string(setmsg[setcount], "set");
    lo_message_add_int32(setmsg[setcount], (int)(blob->id));
    lo_message_add_float(setmsg[setcount], x);
    lo_message_add_float(setmsg[setcount], y);
    lo_message_add_float(setmsg[setcount], (float)0);
    lo_message_add_float(setmsg[setcount], (float)0);
    lo_message_add_float(setmsg[setcount], (float)0);
    lo_bundle_add_message(bundle, "/tuio/2Dcur", setmsg[setcount]);
    setcount++;
    /* enought for a bundle, send it */
    if (setcount >= MAX_BUNDLE_SET) {
      lo_bundle_add_message(bundle, "/tuio/2Dcur", fseqmsg);
      lo_send_bundle(priv->loaddress, bundle);
      /* free set and bundle */
      for (i = 0; i < setcount; i++) {
        lo_message_free(setmsg[i]);
      }
      lo_bundle_free(bundle);
      /* create a new bundle */
      bundle = lo_bundle_new(LO_TT_IMMEDIATE);
      lo_bundle_add_message(bundle, "/tuio/2Dcur", alivemsg);
      setcount = 0;
    }
  }

  lo_bundle_add_message(bundle, "/tuio/2Dcur", fseqmsg);
  lo_send_bundle(priv->loaddress, bundle);
  lo_message_free(alivemsg);
  lo_message_free(fseqmsg);
  for (i = 0; i < setcount; i++) {
    lo_message_free(setmsg[i]);
  }
  lo_bundle_free(bundle);
}

static GstPad *
gst_blobs_to_tuio_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name)
{
  GstPad *srcpad;
  GstBlobsToTUIO *blobtuio;
  int pad_index;
  GstBlobsToTUIOPrivate *priv;

  blobtuio = GST_BLOBSTOTUIO (element);
  priv = GST_BLOBSTOTUIO_GET_PRIVATE (blobtuio);
  
  GST_DEBUG_OBJECT (blobtuio, "requesting pad");

  if (!name || !strcmp(name, "srcthreshold")) {
    pad_index = THRESHOLD_SRC_PAD;
    name = "srcthreshold";
  } else if (!strcmp(name, "srcsmooth")) {
    pad_index = SMOOTH_SRC_PAD;
  } else if (!strcmp(name, "srchighpass")) {
    pad_index = HIGHPASS_SRC_PAD;
  } else if (!strcmp(name, "srcamplify")) {
    pad_index = AMPLIFY_SRC_PAD;
  } else if (!strcmp(name, "srcbg")) {
    pad_index = BG_SRC_PADi;
  } else {
    GST_DEBUG_OBJECT (blobtuio, "name not match");
    return NULL;
  }

  /* Ensure there is only one pad per one type */
  if (priv->processing_srcpad[pad_index] != NULL)
    return NULL;
  
  srcpad = gst_pad_new_from_template (templ, name);
  
  gst_pad_activate_push(srcpad, TRUE);

#if 0
  gst_pad_set_setcaps_function (srcpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_setcaps));
  gst_pad_set_getcaps_function (srcpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_getcaps));
  gst_pad_set_activatepull_function (srcpad,
      GST_DEBUG_FUNCPTR (gst_tee_src_activate_pull));
  gst_pad_set_checkgetrange_function (srcpad,
      GST_DEBUG_FUNCPTR (gst_tee_src_check_get_range));
  gst_pad_set_getrange_function (srcpad,
      GST_DEBUG_FUNCPTR (gst_tee_src_get_range));
#endif

  gst_element_add_pad (GST_ELEMENT (blobtuio), srcpad);

  GST_OBJECT_LOCK (blobtuio);
  /* TODO not sure about GST_OBJECT_LOCK */
  priv->processing_srcpad[pad_index] = srcpad;
  GST_OBJECT_UNLOCK (blobtuio);
    
  return srcpad;
} 
  
static void
gst_blobs_to_tuio_release_pad (GstElement * element, GstPad * pad)
{ 
  GstBlobsToTUIO *blobtuio;
  GstBlobsToTUIOPrivate *priv;
  int i;

  blobtuio = GST_BLOBSTOTUIO (element);
  priv = GST_BLOBSTOTUIO_GET_PRIVATE (blobtuio);

  GST_DEBUG_OBJECT (blobtuio, "releasing pad");

  GST_OBJECT_LOCK (blobtuio);
  for (i = 0; i < MAX_SRC_PAD; i++) {
    if (priv->processing_srcpad[i] == pad) {
      priv->processing_srcpad[i] = NULL;
      break;
    }
  }
  GST_OBJECT_UNLOCK (blobtuio);

  gst_pad_set_active (pad, FALSE);

  gst_element_remove_pad (GST_ELEMENT_CAST (blobtuio), pad);
}


/* GObject vmethod implementations */

static void
gst_blobs_to_tuio_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple(element_class,
    "BlobsToTUIO",
    "Generic",
    "Convert an image blobs into TUIO OSC packets",
    "keithmok <ek9852@gmail.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
}

/* initialize the blobstotuio's class */
static void
gst_blobs_to_tuio_class_init (GstBlobsToTUIOClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  g_type_class_add_private (klass, sizeof (GstBlobsToTUIOPrivate));

  gobject_class->finalize = (GObjectFinalizeFunc) gst_blobs_to_tuio_finalize;
  gobject_class->set_property = gst_blobs_to_tuio_set_property;
  gobject_class->get_property = gst_blobs_to_tuio_get_property;

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_blobs_to_tuio_request_new_pad);
  gstelement_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_blobs_to_tuio_release_pad);
      
  g_object_class_install_property (gobject_class, PROP_MATRIX,
      g_param_spec_string ("matrix",
          "Tranform matrix to match screen coordinates",
          "6 coeffs of the 3x2 matrix separated by comma",
          "1,0,0,0,1,0", G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class, PROP_TRACK_DARK,
      g_param_spec_boolean ("trackdark", "Track dark area as blob or not",
          "Track dark area as blob or not",
          FALSE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_SMOOTH, g_param_spec_uint ("smooth",
          "Smooth the source image",
          "Smooth the source image with blur (radius in pixels)",
          0, 128, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_HIGHPASS_BLUR, g_param_spec_uint ("highpass_blur",
          "Blur value for highpass filter before subtraction with original",
          "Blur value for highpass filter before subtraction with original (radius in pixels)",
          0, 128, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_HIGHPASS_NOISE, g_param_spec_uint ("highpass_noise",
          "Blur value for highpass filter after subtraction with original",
          "Blur value for highpass filter after subtraction with original (radius in pixels)",
          0, 128, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_AMPLIFY, g_param_spec_uint ("amplify",
          "Amplify scale (round to nearest power of 2) after self multiplication of the image (0-disable both self multiplication and amplify)",
          "Amplify scale (round to nearest power of 2) after self multiplication of the image (pixel x pixel x amplify / 128) (0-disable both self multiplication and amplify)",
          0, 128, 16, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_THRESHOLD, g_param_spec_uint ("threshold",
          "Threshold for the blob",
          "Threshold for the blob (0-255)",
          0, 255, 127, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_LEARN_BACKGROUND_COUNTER, g_param_spec_uint (
          "learn_background_counter",
          "Frame countdown counter for background learning",
          "Frame countdown counter for background learning",
          0, G_MAXUINT, 60, G_PARAM_READWRITE));

#if !defined(G_OS_WIN32)
  g_object_class_install_property (gobject_class, PROP_UINPUT,
      g_param_spec_boolean ("uinput", "Enable user space linux input or not",
          "Enable user space linux input or not",
          FALSE, G_PARAM_READWRITE));

#if defined(USE_MT_EVENT)
  g_object_class_install_property (gobject_class, PROP_UINPUT_MT,
      g_param_spec_boolean ("uinput-mt", "Enable user space linux mulitouch event or not",
          "Enable user space linux multitouch event or not",
          TRUE, G_PARAM_READWRITE));
#endif

  g_object_class_install_property (gobject_class, PROP_UINPUT_ABS_X_RANGE,
      g_param_spec_string ("uinput-abs-x-range", "The transformed abs x range for uinput (%%d,%%d)",
          "The transformed abs x range for uinput (%%dx%%d)",
          "0,1024", G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class, PROP_UINPUT_ABS_Y_RANGE,
      g_param_spec_string ("uinput-abs-y-range", "The transformed abs y range for uinput (%%d,%%d)",
          "The transformed abs y range for uinput (%%dx%%d)",
          "0,768", G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class, PROP_UINPUT_DEVNAME,
      g_param_spec_string ("uinput-devname", "Device name for userspace input device",
          "Device name for userspace input device",
          "/dev/uinput", G_PARAM_READWRITE));
#endif

  g_object_class_install_property (gobject_class, PROP_TUIO,
      g_param_spec_boolean ("tuio", "Enable tuio output or not",
          "Enable tuio output or not",
          TRUE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_ADDRESS,
      g_param_spec_string ("address", "Destination address (IP or name)",
          "Destination address (IP or name)",
          "127.0.0.1", G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_PORT,
      g_param_spec_string ("port", "Destination UDP port",
          "Destination UDP port",
          "3333", G_PARAM_READWRITE));
          
  g_object_class_install_property (gobject_class,
      PROP_SURFACEMIN, g_param_spec_uint ("surface-min",
          "Blob min surface",
          "Blob min surface (in pixels)",
          0, G_MAXUINT, 5, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_SURFACEMAX, g_param_spec_uint ("surface-max",
          "Blob max surface",
          "Blob max surface (in pixels)",
          0, G_MAXUINT, 200, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_DISTANCEMAX, g_param_spec_uint ("distance-max",
          "Blob max distance between 2 frames",
          "Blob max distance between 2 frames (in pixels)",
          0, G_MAXUINT, 40, G_PARAM_READWRITE));
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_blobs_to_tuio_init (GstBlobsToTUIO * blobtuio,
    GstBlobsToTUIOClass * gclass)
{
  GstPad *sinkpad;
  GstBlobsToTUIOPrivate *priv = GST_BLOBSTOTUIO_GET_PRIVATE (blobtuio);

  sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_chain_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_blobs_to_tuio_chain));
  gst_pad_set_setcaps_function (sinkpad,
      gst_blobs_to_tuio_set_caps);

  gst_element_add_pad (GST_ELEMENT (blobtuio), sinkpad);

#if 0
  gst_pad_set_event_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_blobs_to_tuio_handle_sink_event));
  gst_pad_set_bufferalloc_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_blobs_to_tuio_buffer_alloc));
#endif

  GST_DEBUG_OBJECT(blobtuio, "gst_blobs_to_tuio_init\n");
  priv->markbuf = NULL;
  priv->background_buf = NULL;
  priv->background_buf_fractional = NULL;
  priv->background_buf_learning_init_counter = 60;
  priv->working_buf1 = NULL;
  priv->working_buf2 = NULL;
  priv->image_blur_temp = NULL;

  priv->blobs = NULL;

  priv->matrix[0] = 1; 
  priv->matrix[1] = 0;
  priv->matrix[2] = 0;
  priv->matrix[3] = 0;
  priv->matrix[4] = 1;
  priv->matrix[5] = 0;
  
  priv->trackdark = FALSE;
  priv->smooth = 0;
  priv->highpass_blur = 0;
  priv->highpass_noise = 0;
  priv->amplify_shift = 3;
  priv->threshold = 127;
#if !defined(G_OS_WIN32)
  priv->uinput = FALSE;
  priv->uinput_devname = g_strdup("/dev/uinput");
#if defined(USE_MT_EVENT)
  priv->uinput_mt = TRUE;
#endif
  priv->uinput_up = TRUE;
  priv->uinput_maxx = 1024;
  priv->uinput_minx = 0;
  priv->uinput_maxy = 768;
  priv->uinput_miny = 0;
  priv->ufile = -1;
#endif
  priv->tuio = TRUE;
  priv->loaddress = lo_address_new("127.0.0.1", "3333");
  priv->address = NULL;
  priv->port = NULL;
 
  priv->surface_min = 30;
  priv->surface_max = 450;
  priv->distance_max = 40;
}

static gboolean
parse_matrix (GstBlobsToTUIOPrivate * priv, const gchar* matrix)
{
  return (sscanf (matrix, "%f,%f,%f,%f,%f,%f",
    &(priv->matrix[0]), &(priv->matrix[1]), &(priv->matrix[2]),
    &(priv->matrix[3]), &(priv->matrix[4]), &(priv->matrix[5])) == 6);
}

#if !defined(G_OS_WIN32)
static void
gst_blobs_to_tuio_set_uinput(GstBlobsToTUIOPrivate *priv, gboolean value)
{
  int ret;
  if (priv->ufile >= 0) {
    close(priv->ufile);
    priv->ufile = -1;
  }
  if ((value)&&(priv->uinput_devname)) {
    struct uinput_user_dev uinp;
    priv->ufile = open(priv->uinput_devname, O_WRONLY | O_NDELAY);

    if (priv->ufile < 0) {
      GST_DEBUG_OBJECT(priv, "Cannot open %d\nMake sure kernel uinput module is inserted and write permission granted\n", priv->uinput_devname);
      priv->uinput = FALSE;
      return;
    }
      
    memset(&uinp, 0, sizeof(uinp));
    strncpy(uinp.name, "webcam to mouse", 20);
    uinp.id.version = 4;
    uinp.id.bustype = BUS_USB;

#if defined(USE_MT_EVENT)
    if (priv->uinput_mt) {
      ioctl(priv->ufile, UI_SET_EVBIT, EV_ABS);
      ioctl(priv->ufile, UI_SET_ABSBIT, ABS_MT_TOUCH_MAJOR);
      ioctl(priv->ufile, UI_SET_ABSBIT, ABS_MT_POSITION_X);
      ioctl(priv->ufile, UI_SET_ABSBIT, ABS_MT_POSITION_Y);
      ioctl(priv->ufile, UI_SET_ABSBIT, ABS_MT_TRACKING_ID);
      uinp.absmax[ABS_MT_TOUCH_MAJOR] = 65535; /* REVISIT */
      uinp.absmin[ABS_MT_TOUCH_MAJOR] = 0;
      uinp.absmax[ABS_MT_POSITION_X] = priv->uinput_maxx;
      uinp.absmin[ABS_MT_POSITION_X] = priv->uinput_minx;
      uinp.absmax[ABS_MT_POSITION_Y] = priv->uinput_maxy;
      uinp.absmin[ABS_MT_POSITION_Y] = priv->uinput_miny;
      uinp.absmax[ABS_MT_TRACKING_ID] = INT_MAX;
      uinp.absmin[ABS_MT_TRACKING_ID] = -INT_MAX;
    } else {
#else
    if (1) {
#endif
      ioctl(priv->ufile, UI_SET_EVBIT, EV_KEY);
      ioctl(priv->ufile, UI_SET_KEYBIT, BTN_MOUSE);
      ioctl(priv->ufile, UI_SET_EVBIT, EV_ABS);
      ioctl(priv->ufile, UI_SET_ABSBIT, ABS_X);
      ioctl(priv->ufile, UI_SET_ABSBIT, ABS_Y);
#if defined(USE_ABS_PRESSURE)
      ioctl(priv->ufile, UI_SET_ABSBIT, ABS_PRESSURE);
      uinp.absmax[ABS_PRESSURE] = 65535;
      uinp.absmin[ABS_PRESSURE] = 0;
#endif
      uinp.absmax[ABS_X] = priv->uinput_maxx;
      uinp.absmin[ABS_X] = priv->uinput_minx;
      uinp.absmax[ABS_Y] = priv->uinput_maxy;
      uinp.absmin[ABS_Y] = priv->uinput_miny;
    }

    /* create input device in input subsystem */
    ret = write(priv->ufile, &uinp, sizeof(uinp));

    ioctl(priv->ufile, UI_DEV_CREATE);
  }
  priv->uinput = value;
}
#endif

static void
gst_blobs_to_tuio_set_amplify(GstBlobsToTUIOPrivate *priv, guint value)
{
  if (value == 0) {
    priv->amplify_shift = 8;
    return;
  }

  if (value >=128) {
    priv->amplify_shift = 0;
    return;
  }
  
  /* value must > 0 and < 128 */
  priv->amplify_shift = 7;
  while (value >= 2) {
    priv->amplify_shift--;
    value /= 2;
  }
}

static void
gst_blobs_to_tuio_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  const gchar* str;
  GstBlobsToTUIO *blobtuio = GST_BLOBSTOTUIO (object);
  GstBlobsToTUIOPrivate *priv = GST_BLOBSTOTUIO_GET_PRIVATE (blobtuio);
  
  switch (prop_id) {
    case PROP_MATRIX:
      str = g_value_get_string (value);
      parse_matrix (priv, str);
      break;
    case PROP_TRACK_DARK:
      priv->trackdark = g_value_get_boolean(value);
      break;
    case PROP_SMOOTH:
      priv->smooth = g_value_get_uint(value);
      break;
    case PROP_HIGHPASS_BLUR:
      priv->highpass_blur = g_value_get_uint(value);
      break;
    case PROP_HIGHPASS_NOISE:
      priv->highpass_noise = g_value_get_uint(value);
      break;
    case PROP_AMPLIFY:
      gst_blobs_to_tuio_set_amplify(priv, g_value_get_uint(value));
      break;
    case PROP_THRESHOLD:
      priv->threshold = g_value_get_uint(value);
      break;
    case PROP_LEARN_BACKGROUND_COUNTER:
      priv->background_buf_learning_init_counter = g_value_get_uint(value);
      break;
#if !defined(G_OS_WIN32)
    case PROP_UINPUT:
      gst_blobs_to_tuio_set_uinput(priv, g_value_get_boolean(value));
      break;
    case PROP_UINPUT_DEVNAME:
      if (priv->uinput_devname)
        g_free(priv->uinput_devname);
      str = g_value_get_string(value);
      priv->uinput_devname = g_strdup(str);
      gst_blobs_to_tuio_set_uinput(priv, priv->uinput);
      break;
#if defined(USE_MT_EVENT)
    case PROP_UINPUT_MT:
      priv->uinput_mt = g_value_get_boolean(value);
      gst_blobs_to_tuio_set_uinput(priv, priv->uinput);
      break;
#endif
    case PROP_UINPUT_ABS_X_RANGE:
      sscanf (g_value_get_string(value), "%d,%d", &priv->uinput_minx,
          &priv->uinput_maxx);
      gst_blobs_to_tuio_set_uinput(priv, priv->uinput);
      break;
    case PROP_UINPUT_ABS_Y_RANGE:
      sscanf (g_value_get_string(value), "%d,%d", &priv->uinput_miny,
          &priv->uinput_maxy);
      gst_blobs_to_tuio_set_uinput(priv, priv->uinput);
      break;
#endif
    case PROP_TUIO:
      priv->tuio = g_value_get_boolean(value);
      break;
    case PROP_ADDRESS:
      str = g_value_get_string (value);
      if (priv->address)
        g_free(priv->address);
      priv->address = g_strdup(str);
      lo_address_free(priv->loaddress);
      priv->loaddress = lo_address_new(priv->address,
          (priv->port == NULL) ? "3333" : priv->port);
      break;
    case PROP_PORT:
      str = g_value_get_string (value);
      if (priv->port)
        g_free(priv->port);
      priv->port = g_strdup(str);
      lo_address_free(priv->loaddress);
      priv->loaddress = lo_address_new((priv->address == NULL) ? "127.0.0.1" : 
          priv->address, priv->port);
      break;
    case PROP_SURFACEMIN:
      priv->surface_min = g_value_get_uint (value);
      break;
    case PROP_SURFACEMAX:
      priv->surface_max = g_value_get_uint (value);
      break;
    case PROP_DISTANCEMAX:
      priv->distance_max = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* transform amplify shift to amplify */
static guint
gst_blobs_to_tuio_get_amplify(int amplify_shift)
{
  int i;
  guint amplify;

  if (amplify_shift >= 8)
    return 0;

  amplify = 1;
  for (i=0;i<(7-amplify_shift);i++) {
    amplify *= 2;
  }
  return amplify;
}

static void
gst_blobs_to_tuio_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstBlobsToTUIO *blobtuio = GST_BLOBSTOTUIO (object);
  GstBlobsToTUIOPrivate *priv= GST_BLOBSTOTUIO_GET_PRIVATE (blobtuio);
      
  switch (prop_id) {
    case PROP_TRACK_DARK:
      g_value_set_boolean (value, priv->trackdark);
      break;
    case PROP_SMOOTH:
      g_value_set_uint(value, priv->smooth);
      break;
    case PROP_HIGHPASS_BLUR:
      g_value_set_uint(value, priv->highpass_blur);
      break;
    case PROP_HIGHPASS_NOISE:
      g_value_set_uint(value, priv->highpass_noise);
      break;
    case PROP_AMPLIFY:
      g_value_set_uint(value, gst_blobs_to_tuio_get_amplify(priv->amplify_shift));
      break;
    case PROP_THRESHOLD:
      g_value_set_uint(value, priv->threshold);
      break;
    case PROP_LEARN_BACKGROUND_COUNTER:
      g_value_set_uint(value, priv->background_buf_learning_init_counter);
      break;
#if !defined(G_OS_WIN32)
    case PROP_UINPUT:
      g_value_set_boolean (value, priv->uinput);
      break;
    case PROP_UINPUT_DEVNAME:
      g_value_set_string (value, priv->uinput_devname);
      break;
#if defined(USE_MT_EVENT)
    case PROP_UINPUT_MT:
      g_value_set_boolean (value, priv->uinput_mt);
      break;
#endif
#endif
    case PROP_TUIO:
      g_value_set_boolean (value, priv->tuio);
      break;
    case PROP_ADDRESS:
      g_value_set_string (value, priv->address);
      break;
    case PROP_PORT:
      g_value_set_string (value, priv->port);
      break;
    case PROP_SURFACEMIN:
      g_value_set_uint (value, priv->surface_min);
      break;
    case PROP_SURFACEMAX:
      g_value_set_uint (value, priv->surface_max);
      break;
    case PROP_DISTANCEMAX:
      g_value_set_uint (value, priv->distance_max);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_blobs_to_tuio_finalize (GstBlobsToTUIO * blobtuio)
{
  GstBlobsToTUIOPrivate *priv = GST_BLOBSTOTUIO_GET_PRIVATE (blobtuio);

  if (priv->markbuf != NULL)
    g_free(priv->markbuf);

  if (priv->background_buf!= NULL)
    g_free(priv->background_buf);

  if (priv->background_buf_fractional!= NULL)
    g_free(priv->background_buf_fractional);

  if (priv->working_buf1 != NULL)
    g_free(priv->working_buf1 );

  if (priv->working_buf2 != NULL)
    g_free(priv->working_buf2);

  if (priv->image_blur_temp!= NULL)
    g_free(priv->image_blur_temp);

  if (priv->sinkpad)
    g_object_unref(priv->sinkpad);

  g_slist_foreach(priv->blobs, (GFunc)g_free, NULL);
  g_slist_free(priv->blobs);

  lo_address_free(priv->loaddress);

#if !defined(G_OS_WIN32)
  if (priv->ufile < 0)
    close(priv->ufile);
  if (priv->uinput_devname != NULL)
    g_free(priv->uinput_devname);
#endif
  if (priv->address != NULL)
    g_free(priv->address);
  if (priv->port != NULL)
    g_free(priv->port);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (blobtuio));
}

static inline void
swap_image_pointer(guint8 **img1, guint8 **img2)
{
  guint8 *img;
  img = *img1;
  *img1 = *img2;
  *img2 = img;
}

static GstFlowReturn
gst_blobs_to_tuio_src_processing_image(GstBlobsToTUIO *blobtuio, GstPad *pad,
  GstBuffer *buf, void *image_buf, int size,
  void(*convert_function)(GstBlobsToTUIOPrivate *priv, void *targetbuf, const void *image_buf, int width, int height))
{
  GstBuffer *newbuf;
  GstFlowReturn ret;
  GstCaps *caps;
  GstBlobsToTUIOPrivate *priv;

  priv = GST_BLOBSTOTUIO_GET_PRIVATE(blobtuio);

  caps = gst_buffer_get_caps(buf);
  ret = gst_pad_alloc_buffer_and_set_caps (pad,
      GST_BUFFER_OFFSET_NONE, size, caps, &newbuf);
  gst_caps_unref(caps);
  if (ret != GST_FLOW_OK) {
    GST_WARNING_OBJECT (blobtuio, ""
        "failed: combined flow return: %s", gst_flow_get_name (ret));
    return ret;
  }

  GST_DEBUG_OBJECT (blobtuio, "Prepending");
  gst_buffer_copy_metadata (newbuf, buf, GST_BUFFER_COPY_TIMESTAMPS |
      GST_BUFFER_COPY_FLAGS);
  /* TODO anything we can optimatize here not to copy, alloc buffer everytime */
  if (convert_function)
    convert_function(priv, GST_BUFFER_DATA(newbuf), image_buf, priv->width, priv->height);
  else
    memcpy (GST_BUFFER_DATA (newbuf), image_buf, priv->width * priv->height);

  return gst_pad_push(pad, newbuf);
}

static void
gst_blobs_to_tuio_src_threadhold_convert(GstBlobsToTUIOPrivate *priv,
  void *t, const void *s, int width, int height)
{
  pf_image8_threshold(s, t, width, width, height, priv->threshold);
}

static GstFlowReturn
gst_blobs_to_tuio_chain(GstPad * pad, GstBuffer * buf)
{
  GstBlobsToTUIO *blobtuio;
  GstBlobsToTUIOPrivate *priv;
  GArray *zones;
  guint8 *image_buf;
  guint8 *image_buf_temp;

  blobtuio = GST_BLOBSTOTUIO (gst_pad_get_parent (pad));
  priv = GST_BLOBSTOTUIO_GET_PRIVATE(blobtuio);

  GST_DEBUG_OBJECT(blobtuio, "gst_blobs_to_tuio_render%d\n", GST_BUFFER_SIZE (buf));

  if (priv->background_buf_learning_init_counter) {
    /* copy image to background image */
    /* we learn background until webcam exposure is steady */
    guint8 *p;
    guint8 *b;
    p = GST_BUFFER_DATA(buf);
    b = priv->background_buf;
    priv->background_buf_learning_init_counter--;
    memcpy(b, p, priv->width * priv->height);
    memset(priv->background_buf_fractional, 0, priv->width * priv->height * 2);
  } else {
    /* learning for background image using a fixed scale (~0.0001=~5min@30fps) */
    pf_update_background_buf(GST_BUFFER_DATA(buf), priv->background_buf, priv->background_buf_fractional, priv->width, priv->width, priv->height);
  }
  /* subtract image with learnt background */
  if (priv->trackdark) {
    guint8 *p, *q;
    guint8 *b;
    p = GST_BUFFER_DATA(buf);
    q = priv->working_buf1;
    b = priv->background_buf;
    pf_image8_subtract(b, p, q, priv->width, priv->width, priv->height);
  } else {
    guint8 *p, *q;
    guint8 *b;
    p = GST_BUFFER_DATA(buf);
    q = priv->working_buf1;
    b = priv->background_buf;
    pf_image8_subtract(p, b, q, priv->width, priv->width, priv->height);
  }

  if (priv->processing_srcpad[BG_SRC_PADi]) {
    gst_blobs_to_tuio_src_processing_image(blobtuio, priv->processing_srcpad[BG_SRC_PADi],
      buf, priv->background_buf, priv->width*priv->height, NULL);
  }

  image_buf = priv->working_buf1;
  image_buf_temp = priv->working_buf2;

  if (priv->smooth) {
    pf_image8_box_blur(image_buf, image_buf_temp, priv->width, priv->width, priv->height, priv->image_blur_temp, priv->smooth);
    swap_image_pointer(&image_buf, &image_buf_temp);
  }

  if (priv->processing_srcpad[SMOOTH_SRC_PAD]) {
    gst_blobs_to_tuio_src_processing_image(blobtuio, priv->processing_srcpad[SMOOTH_SRC_PAD],
      buf, image_buf, priv->width*priv->height, NULL);
  }

  if (priv->highpass_blur) {
    /* blur = lowpass filter, we subtract the orignal image with lowpass image to get a highpass image */
    pf_image8_box_blur(image_buf, image_buf_temp, priv->width, priv->width, priv->height, priv->image_blur_temp, priv->highpass_blur);
    pf_image8_subtract(image_buf, image_buf_temp, image_buf, priv->width, priv->width, priv->height);
    /* since noise also highpassed we need blur again to minimize it */
    if (priv->highpass_noise) {
      pf_image8_box_blur(image_buf, image_buf_temp, priv->width, priv->width, priv->height, priv->image_blur_temp, priv->highpass_noise);
      swap_image_pointer(&image_buf, &image_buf_temp);
    }
  }

  if (priv->processing_srcpad[HIGHPASS_SRC_PAD]) {
    gst_blobs_to_tuio_src_processing_image(blobtuio, priv->processing_srcpad[HIGHPASS_SRC_PAD],
      buf, image_buf, priv->width*priv->height, NULL);
  }

  if (priv->amplify_shift < 8) {
    pf_image8_amplify(image_buf, image_buf, priv->width, priv->width, priv->height, priv->amplify_shift);
  }

  if (priv->processing_srcpad[AMPLIFY_SRC_PAD]) {
    gst_blobs_to_tuio_src_processing_image(blobtuio, priv->processing_srcpad[AMPLIFY_SRC_PAD],
      buf, image_buf, priv->width*priv->height, NULL);
  }

  if (priv->processing_srcpad[THRESHOLD_SRC_PAD]) {
    gst_blobs_to_tuio_src_processing_image(blobtuio, priv->processing_srcpad[THRESHOLD_SRC_PAD],
      buf, image_buf, priv->width*priv->height, gst_blobs_to_tuio_src_threadhold_convert);
  }

  /* find blobs zones */
  find_zones(image_buf, priv->width, priv->height, priv->threshold, priv->surface_min, priv->surface_max, priv->markbuf, &zones);

#if DEBUG
  {
  int i;
  Zone *zone;
    g_message("=== Total no of zones: %d", zones->len);
    for (i = 0; i < zones->len; i++) {
        zone = &g_array_index(zones, Zone, i);
        g_message("Zone (%d %d) (%d, %d) (%d %d) surface %d", zone->total_x/zone->surface_size, zone->total_y/zone->surface_size,
            zone->xstart, zone->ystart, zone->xend, zone->yend, zone->surface_size);
    }
  }
#endif
  /* update blobs */
  blob_list_update(priv, zones);
#if DEBUG
  {
    GSList *l;
    Blob *b;
    l = priv->blobs;
    g_message("=== Blobs %d", g_slist_length(priv->blobs));
    while (l) {
      b = (Blob *)l->data;
      g_message("tuio: %d %f %f", b->id, b->x, b->y);
      l = g_slist_next(l);
    }
  }
#endif

  g_array_free(zones, TRUE);

#if !defined(G_OS_WIN32)
  if (priv->uinput) {
#if defined(USE_MT_EVENT)
    if (priv->uinput_mt)
      send_uinput_mt(priv);
    else
#endif
      send_uinput(priv);
  }
#endif

  if (priv->tuio)
    send_tuio(priv);

  gst_object_unref (blobtuio);
  gst_buffer_unref (buf);

  return GST_FLOW_OK;
}

#if 0
static GstCaps *
gst_blobs_to_tuio_get_caps (GstPad * pad)
{
  GstStructure *structure;
  GstBlobsToTUIO *blobtuio;
  GstBlobsToTUIOPrivate *private;
  GstCaps *caps[5] = {NULL, };
  int i;

  blobtuio = GST_BLOBSTOTUIO(gst_pad_get_parent (pad));

  private = GST_BLOBSTOTUIO_GET_PRIVATE(blobtuio);
      
  if (bg_srcpad) {
    caps[0] = gst_pad_peer_get_caps(bg_srcpad);
  }
  if (smooth_srcpad) {
    caps[1] = gst_pad_peer_get_caps(smooth_srcpad);
  }
  if (highpass_srcpad) {
    caps[2] = gst_pad_peer_get_caps(highpass_srcpad);
  }
  if (amplify_srcpad) {
    caps[3] = gst_pad_peer_get_caps(amplify_srcpad);
  }
  if (threshold_srcpad) {
    caps[4] = gst_pad_peer_get_caps(threshold_srcpad);
  }

  for (i=0; i<5; i++) {
    if (caps[i]!=NULL) {
      finalcap = gst_caps_intersect();
    }
  }

  for (i=0; i<5; i++) {
    if (caps[i]!=NULL)
      g_object_unref(caps[i]);
  }
}
#endif

static gboolean
gst_blobs_to_tuio_set_caps (GstPad * pad, GstCaps * caps)
{
  GstStructure *structure;
  GstBlobsToTUIO *blobtuio;
  GstBlobsToTUIOPrivate *private;

  blobtuio = GST_BLOBSTOTUIO(gst_pad_get_parent (pad));

  private = GST_BLOBSTOTUIO_GET_PRIVATE(blobtuio);
      
  structure = gst_caps_get_structure(caps, 0);

  /* get the with and height */
  gst_structure_get_int(structure, "width", &(private->width));
  gst_structure_get_int(structure, "height", &(private->height));
 
  /* allocate buffers */
  private->markbuf = (gint*)g_malloc(private->width * private->height * sizeof(gint));
  private->background_buf = (guint8*)g_malloc(private->width * private->height * sizeof(guint8));
  private->background_buf_fractional = (guint16*)g_malloc(private->width * private->height * sizeof(guint16));
  private->working_buf1 = (guint8*)g_malloc(private->width * private->height * sizeof(guint8));
  private->working_buf2 = (guint8*)g_malloc(private->width * private->height * sizeof(guint8));
  private->image_blur_temp = (guint8*)g_malloc(private->width * private->height * sizeof(guint8));

  gst_object_unref (blobtuio);
    
  return TRUE;
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
blobstotuio_init (GstPlugin * blobstotuio)
{
  /* debug category for fltering log messages
   */
  GST_DEBUG_CATEGORY_INIT (gst_blobs_to_tuio_debug, "blobstotuio",
      0, "Generate touch event from grayscale image");

  return gst_element_register (blobstotuio, "blobstotuio", GST_RANK_NONE,
      GST_TYPE_BLOBSTOTUIO);
}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "blobstotuio"
#endif

/* gstreamer looks for this structure to register blobstotuios
 */
GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "blobstotuio",
  "Generate touch event from grayscale image",
  blobstotuio_init,
  VERSION,
  "LGPL",
  "tuio",
  "http://gst-tuio.sourceforge.net"
)

