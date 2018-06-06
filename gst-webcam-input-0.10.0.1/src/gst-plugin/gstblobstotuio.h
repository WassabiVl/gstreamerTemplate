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

#ifndef __GST_BLOBSTOTUIO_H__
#define __GST_BLOBSTOTUIO_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_BLOBSTOTUIO \
  (gst_blobs_to_tuio_get_type())
#define GST_BLOBSTOTUIO(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BLOBSTOTUIO,GstBlobsToTUIO))
#define GST_BLOBSTOTUIO_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_BLOBSTOTUIO,GstBlobsToTUIOClass))
#define GST_IS_BLOBSTOTUIO(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BLOBSTOTUIO))
#define GST_IS_BLOBSTOTUIO_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_BLOBSTOTUIO))

typedef struct _GstBlobsToTUIO      GstBlobsToTUIO;
typedef struct _GstBlobsToTUIOClass GstBlobsToTUIOClass;
typedef struct _GstBlobsToTUIOPrivate GstBlobsToTUIOPrivate;

struct _GstBlobsToTUIO
{
  GstElement element;

  GstBlobsToTUIOPrivate *priv;
};

struct _GstBlobsToTUIOClass 
{
  GstElementClass parent_class;
};

GType gst_blobs_to_tuio_get_type (void);

G_END_DECLS

#endif /* __GST_BLOBSTOTUIO_H__ */
