#include <gst/gst.h>

static gchar *opt_effects = NULL;

#define DEFAULT_PORTS "5002,5003"

static GstPad *blockpad;
static GstElement *last_ele;
static GstElement *pipeline;
static GstElement *videomixer;
static GQueue current_ports = G_QUEUE_INIT;

static GstPadProbeReturn event_probe_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
    GMainLoop *loop = user_data;
    GstElement *next;

    if (GST_EVENT_TYPE (GST_PAD_PROBE_INFO_DATA (info)) != GST_EVENT_EOS)
    {
        return GST_PAD_PROBE_PASS;
    }

    gst_pad_remove_probe (pad, GST_PAD_PROBE_INFO_ID (info));

    /* take next port from the queue */
    next = g_queue_pop_head (&current_ports);

    if (next == NULL)
    {
        GST_DEBUG_OBJECT (pad, "no more ports");
        g_main_loop_quit (loop);
        return GST_PAD_PROBE_DROP;
    }

    //g_print ("Switching from '%s' to '%s'..\n",  cur_port, next);
    g_print ("Adding '%s'\n", next);
//   gst_element_set_state (prev_mixer, GST_STATE_NULL);

    /* remove unlinks automatically */
    //GST_DEBUG_OBJECT (pipeline, "removing %" GST_PTR_FORMAT, prev_mixer);
    // gst_bin_remove (GST_BIN (pipeline), cur_source);

    //GST_DEBUG_OBJECT (pipeline, "adding   %" GST_PTR_FORMAT, next);

    GstElement *src_new, *rtpvp8depay_new, *vp8dec_new, *alpha_new;
    src_new = gst_element_factory_make ("udpsrc", NULL);
    g_object_set (src_new, "port", *next, NULL);
    gst_util_set_object_arg (G_OBJECT (src_new), "caps", "application/x-rtp");

    rtpvp8depay_new = gst_element_factory_make ("rtpvp8depay", NULL);

    vp8dec_new = gst_element_factory_make ("vp8dec", NULL);

    alpha_new = gst_element_factory_make ("alpha", NULL);
    gst_util_set_object_arg (G_OBJECT (alpha_new), "method", "green");

    GST_DEBUG_OBJECT (pipeline, "adding..");
    g_print ("Add many\n");
    gst_bin_add_many (GST_BIN (pipeline), src_new, rtpvp8depay_new, vp8dec_new, alpha_new, NULL);

    GST_DEBUG_OBJECT (pipeline, "linking..");
    g_print ("linking\n");
    gst_element_link_many (last_ele, src_new, rtpvp8depay_new, vp8dec_new, alpha_new, videomixer, NULL);

    last_ele = alpha_new;

    GST_DEBUG_OBJECT (pipeline, "setting state..");
    g_print ("setting state\n");
    gst_element_set_state (src_new, GST_STATE_PLAYING);
    gst_element_set_state (rtpvp8depay_new, GST_STATE_PLAYING);
    gst_element_set_state (vp8dec_new, GST_STATE_PLAYING);
    gst_element_set_state (alpha_new, GST_STATE_PLAYING);

    g_print ("done\n");
    GST_DEBUG_OBJECT (pipeline, "done");

    return GST_PAD_PROBE_DROP;
}

static GstPadProbeReturn pad_probe_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
    g_print ("begin pad_probe_cb\n");
    GstPad *srcpad, *sinkpad;

    if (srcpad == NULL)
    {
        g_print ("srcpad null\n");
    }
    else
    {
        g_print ("srcpad not null\n");
    }

    GST_DEBUG_OBJECT (pad, "pad is blocked now");

    /* remove the probe first */
    gst_pad_remove_probe (pad, GST_PAD_PROBE_INFO_ID (info));

    if (srcpad == NULL)
    {
        g_print ("srcpad null\n");
    }

    /* install new probe for EOS */
    srcpad = gst_element_get_static_pad (last_ele, "src");

    gst_pad_add_probe (srcpad, GST_PAD_PROBE_TYPE_BLOCK | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, event_probe_cb, user_data, NULL);
    gst_object_unref (srcpad);

    /* push EOS into the element, the probe will be fired when the
     * EOS leaves the effect and it has thus drained all of its data */
    sinkpad = gst_element_get_static_pad (last_ele, "sink");
    gst_pad_send_event (sinkpad, gst_event_new_eos ());
    gst_object_unref (sinkpad);

    return GST_PAD_PROBE_OK;
}

static gboolean
timeout_cb (gpointer user_data)
{
    g_print ("begin timeout_cb\n");
    gst_pad_add_probe (blockpad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM, pad_probe_cb, user_data, NULL);
    g_print ("end timeout_cb\n");
    return TRUE;
}

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

int main (int argc, char **argv)
{
    GOptionEntry options[] =
    {
        {
            "Ports", 'e', 0, G_OPTION_ARG_STRING, &opt_effects,
            "Ports to use (comma-separated list of ports)", NULL
        },

        {NULL}
    };

    GOptionContext *ctx;
    GError *err = NULL;
    GMainLoop *loop;
    gchar **ports, **p;

    ctx = g_option_context_new ("");
    g_option_context_add_main_entries (ctx, options, NULL);
    g_option_context_add_group (ctx, gst_init_get_option_group ());

    if (!g_option_context_parse (ctx, &argc, &argv, &err))
    {
        g_print ("Error initializing: %s\n", err->message);
        //  g_clear_error (&amp, err);
        g_option_context_free (ctx);
        return 1;
    }

    g_option_context_free (ctx);

    if (opt_effects != NULL)
    {
        ports = g_strsplit (opt_effects, ",", -1);
    }
    else
    {
        ports = g_strsplit (DEFAULT_PORTS, ",", -1);
    }

    for (p = ports; p != NULL && *p != NULL; ++p)
    {
        g_queue_push_tail (&current_ports, *p);
    }

    g_print ("Initialising pipeline\n");
    pipeline = gst_pipeline_new ("pipeline");
    GstElement *src, *rtpvp8depay, *vp8dec, *alpha, *q1, *videoconvert, *autovideosink;
    src = gst_element_factory_make ("udpsrc", NULL);
    g_object_set(src, "port", 5000, NULL);
    gst_util_set_object_arg (G_OBJECT (src), "caps", "application/x-rtp");

    rtpvp8depay = gst_element_factory_make ("rtpvp8depay", NULL);

    vp8dec = gst_element_factory_make ("vp8dec", NULL);

    alpha = gst_element_factory_make ("alpha", NULL);
    gst_util_set_object_arg (G_OBJECT (alpha), "method", "green");

    videomixer = gst_element_factory_make ("videomixer", NULL);
    gst_util_set_object_arg (G_OBJECT (videomixer), "name", "mixer");

    q1 = gst_element_factory_make ("queue", NULL);
    blockpad = gst_element_get_static_pad (videomixer, "src");

    videoconvert = gst_element_factory_make ("videoconvert", NULL);

    autovideosink = gst_element_factory_make ("autovideosink", NULL);

    gst_bin_add_many (GST_BIN (pipeline), src, rtpvp8depay, vp8dec, alpha, videomixer, videoconvert, autovideosink, NULL);
    last_ele = alpha;

    gst_element_link_many (src, rtpvp8depay, vp8dec, alpha, videomixer, videoconvert, autovideosink, NULL);

    gst_element_set_state (pipeline, GST_STATE_PLAYING);
    g_print ("Initialised pipeline\n");

    loop = g_main_loop_new (NULL, FALSE);

    gst_bus_add_watch (GST_ELEMENT_BUS (pipeline), bus_cb, loop);

    g_timeout_add_seconds (5, timeout_cb, loop);

    g_main_loop_run (loop);

    gst_element_set_state (pipeline, GST_STATE_NULL);
    gst_object_unref (pipeline);
    return 0;
}
