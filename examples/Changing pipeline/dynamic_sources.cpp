#include <gst/gst.h>

static GstElement *pipeline;

static gboolean bus_cb (GstBus * bus, GstMessage * msg, gpointer user_data)
{
    GMainLoop *loop = user_data;

    switch (GST_MESSAGE_TYPE (msg))
    {
    case GST_MESSAGE_ERROR:
    {
        GError *err = NULL;
        gchar *dbg;

        gst_message_parse_error (msg, &err, &dbg);
        gst_object_default_error (msg->src, err, dbg);
        g_clear_error (&err);
        g_free (dbg);
        g_main_loop_quit (loop);
        break;
    }
    default:
        break;
    }

    return TRUE;
}

static GstElement * add_source(GstElement * first_link, int port)
{
    GstElement *src, *rtpvp8depay, *vp8dec, *alpha;
    src = gst_element_factory_make ("udpsrc", NULL);
    g_object_set(src, "port", port, NULL);
    gst_util_set_object_arg (G_OBJECT (src), "caps", "application/x-rtp");

    rtpvp8depay = gst_element_factory_make ("rtpvp8depay", NULL);

    vp8dec = gst_element_factory_make ("vp8dec", NULL);

    alpha = gst_element_factory_make ("alpha", NULL);
    gst_util_set_object_arg (G_OBJECT (alpha), "method", "green");

    gst_bin_add_many (GST_BIN (pipeline), src, rtpvp8depay, vp8dec, alpha, NULL);

    if (first_link != NULL)
    {
        gst_element_link(first_link, src);
    }

    gst_element_link_many (src, rtpvp8depay, vp8dec, alpha, NULL);
    return alpha;
}

int main (int argc, char **argv)
{
    g_print ("Initialising pipeline\n");
    gst_init (&argc, &argv);
    pipeline = gst_pipeline_new ("pipeline");
    GstElement * alpha =  add_source(NULL, 5000);
    GstElement *videomixer, *videoconvert, *autovideosink;

    videomixer = gst_element_factory_make ("videomixer", NULL);
    gst_util_set_object_arg (G_OBJECT (videomixer), "name", "mixer");

    videoconvert = gst_element_factory_make ("videoconvert", NULL);

    autovideosink = gst_element_factory_make ("autovideosink", NULL);

    gst_bin_add_many (GST_BIN (pipeline), videomixer, videoconvert, autovideosink, NULL);

    gst_element_link_many (alpha, videomixer, videoconvert, autovideosink, NULL);

    gst_element_set_state (pipeline, GST_STATE_PLAYING);
    g_print ("Initialised pipeline\n");

    GMainLoop *loop = g_main_loop_new (NULL, FALSE);
    gst_bus_add_watch (GST_ELEMENT_BUS (pipeline), bus_cb, loop);

    g_main_loop_run (loop);

    gst_element_set_state (pipeline, GST_STATE_NULL);
    gst_object_unref (pipeline);
    return 0;
}
