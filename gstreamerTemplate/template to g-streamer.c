#include <gst/gst.h>

/**
 * Structure to contain all our information,
 * so we can pass it to callbacks
 **/
typedef struct _CustomData {
	GstElement *pipeline;
	GstElement *source;
	GstElement *convert;
	GstElement *sink;
	GstElement *playbin;   /* the simple way to start a pipeline */
	gboolean playing;      /* Are we in the PLAYING state? */
	gboolean terminate;    /* Should we terminate execution? */
	gboolean seek_enabled; /* Is seeking enabled for this media? */
	gboolean seek_done;    /* Have we performed the seek already? */
	gint64 duration;       /* How long does this media last, in nanoseconds */
} CustomData;

/*************************************************************************
 * Handler for the pad-added signal
 * Since this tutorial (and most real applications) involves callbacks,
 * we will group all our data in a structure for easier handling.
 *************************************************************************/
static void pad_added_handler (GstElement *src, GstPad *pad, CustomData *data);
/**
 * This function will be called by the pad-added signal
 * if time is needed to process the convertor
 * src is the GstElement which triggered the signal.
 * new_pad is the GstPad that has just been added to the src element
 * data is the pointer we provided when attaching to the signal
 **/
static void pad_added_handler (GstElement *src, GstPad *new_pad, CustomData *data) {
	GstPad *sink_pad = gst_element_get_static_pad (data->convert, "sink");
	GstPadLinkReturn ret;
	GstCaps *new_pad_caps = NULL;
	GstStructure *new_pad_struct = NULL;
	const gchar *new_pad_type = NULL;

	g_print ("Received new pad '%s' from '%s':\n", GST_PAD_NAME (new_pad), GST_ELEMENT_NAME (src));

	/**
	 * Previous we linked element against element,this one links them directly
	 * If our converter is already linked, we have nothing to do here
	 * */
	if (gst_pad_is_linked (sink_pad)) {
		g_print ("We are already linked. Ignoring.\n");
		goto exit;
	}

	/**
	 * Check the new pad's type
	 * uridecodebin can create as many pads as it sees fit,
	 * and for each one, this callback will be called.
	 * These lines of code will prevent us from trying to link to a new pad once we are already linked.
	 **/
	// gst_pad_get_current_caps() retrieves the current capabilities of the pad (that is, the kind of data it currently outputs), wrapped in a GstCaps structure
	new_pad_caps = gst_pad_get_current_caps (new_pad);
	// we know that the pad we want only had one capability (audio), we retrieve the first GstStructure with gst_caps_get_structure()
	new_pad_struct = gst_caps_get_structure (new_pad_caps, 0);
	//gst_structure_get_name() we recover the name of the structure, which contains the main description of the format (its media type, actually)
	new_pad_type = gst_structure_get_name (new_pad_struct);
	if (!g_str_has_prefix (new_pad_type, "audio/x-raw")) {
		g_print ("It has type '%s' which is not raw audio. Ignoring.\n", new_pad_type);
		goto exit;
	}

	/**
	 * Attempt the link
	 * gst_pad_link() tries to link two pads. As it was the case with gst_element_link(),
	 * the link must be specified from source to sink, and both pads must be owned by elements residing in the same bin (or pipeline).
	 **/
	ret = gst_pad_link (new_pad, sink_pad);
	if (GST_PAD_LINK_FAILED (ret)) {
		g_print ("Type is '%s' but link failed.\n", new_pad_type);
	} else {
		g_print ("Link succeeded (type '%s').\n", new_pad_type);
	}

	exit:
	/* Unreference the new pad's caps, if we got them */
	if (new_pad_caps != NULL)
		gst_caps_unref (new_pad_caps);

	/* Unreference the sink pad */
	gst_object_unref (sink_pad);
}

/*****************************************************************************
 * Forward definition of the message processing function
 ****************************************************************************/
static void handle_message (CustomData *data, GstMessage *msg);
/**
 * to handle the messages, they have been removed to an eternal function
 */
static void handle_message (CustomData *data, GstMessage *msg) {
	GError *err;
	gchar *debug_info;

	switch (GST_MESSAGE_TYPE (msg)) {
		case GST_MESSAGE_ERROR:
			gst_message_parse_error (msg, &err, &debug_info);
			g_printerr ("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), err->message);
			g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
			g_clear_error (&err);
			g_free (debug_info);
			data->terminate = TRUE;
			break;
		case GST_MESSAGE_EOS:
			g_print ("End-Of-Stream reached.\n");
			data->terminate = TRUE;
			break;
		case GST_MESSAGE_DURATION:
			/* The duration has changed, mark the current one as invalid */
			data->duration = GST_CLOCK_TIME_NONE;
			break;
			/**
			 * We are only interested in state-changed messages from the pipeline
			 * the current states of g-streamer are found here
			 * https://gstreamer.freedesktop.org/documentation/#gstreamer-states
			 **/
		case GST_MESSAGE_STATE_CHANGED: {
			GstState old_state, new_state, pending_state;
			gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
			if (GST_MESSAGE_SRC (msg) == GST_OBJECT (data->playbin)) {
				g_print ("Pipeline state changed from %s to %s:\n",
				         gst_element_state_get_name (old_state), gst_element_state_get_name (new_state));

				/* Remember whether we are in the PLAYING state or not */
				data->playing = (new_state == GST_STATE_PLAYING);

				if (data->playing) {
					/* We just moved to PLAYING. Check if seeking is possible */
					GstQuery *query;
					gint64 start, end;
					query = gst_query_new_seeking (GST_FORMAT_TIME);
					if (gst_element_query (data->playbin, query)) {
						gst_query_parse_seeking (query, NULL, &data->seek_enabled, &start, &end);
						if (data->seek_enabled) {
							g_print ("Seeking is ENABLED from %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT "\n",
							         GST_TIME_ARGS (start), GST_TIME_ARGS (end));
						} else {
							g_print ("Seeking is DISABLED for this stream.\n");
						}
					}
					else {
						g_printerr ("Seeking query failed.");
					}
					gst_query_unref (query);
				}
			}
		} break;
		default:
			/* We should not reach here */
			g_printerr ("Unexpected message received.\n");
			break;
	}
	gst_message_unref (msg);
}


/**************************************************************************
 * The Main Function that calls g-streamer
 *************************************************************************/
int main(int argc, char *argv[]) {
	/* variables to make g-streamer work */
	CustomData data;
	GstBus *bus;
	GstMessage *msg;
	GstStateChangeReturn ret;
	gboolean terminate = FALSE;
	//
	data.playing = FALSE;
	data.terminate = FALSE;
	data.seek_enabled = FALSE;
	data.seek_done = FALSE;
	data.duration = GST_CLOCK_TIME_NONE;

	/** 1) Initialize GStreamer
	 This is the very first step */
	gst_init (&argc, &argv);

	/**
	 * 2) Create the elements that are needed
	 * using gst_element_factory_make
	 * Source is the input, sink is the output
	 **/
	// the input needed
	// data.source = gst_element_factory_make ("uridecodebin", "source"); //the method to use Uri/URL
	data.source = gst_element_factory_make ("videotestsrc", "source");
	// the convector element needed
	data.convert = gst_element_factory_make ("audioconvert", "convert");
	// the output element needed
	//   data.sink = gst_element_factory_make ("autoaudiosink", "sink"); //this is for audio
	data.sink = gst_element_factory_make ("autovideosink", "sink");

	/**
	 * 3) Create the empty pipeline
	 **/
	data.pipeline = gst_pipeline_new ("test-pipeline");

	/**
	 * 4) check if the pipeline or elements have problems
	 **/
	if (!data.pipeline) {
		g_printerr ("pipeline could not be created.\n");
		return -1;
	}else if (!data.source) {
		g_printerr ("source could not be created.\n");
		return -1;
	} else if (!data.sink) {
		g_printerr ("sink could not be created.\n");
		return -1;
	}else if (!data.convert) {
		g_printerr ("converter could not be created.\n");
		return -1;
	}

	/**
	 * 5) Build the pipeline
	 **/
	// simple version:
	//	pipeline = gst_parse_launch ("playbin uri=https://www.freedesktop.org/software/gstreamer-sdk/data/media/sintel_trailer-480p.webm", NULL);
	// building with multiple items
	gst_bin_add_many (GST_BIN (data.pipeline), data.source, data.convert, data.sink, NULL);

	/**
	 * 6) check if the output (sink) and input (source) can be linked
	 * or if the converter can be linked to the output
	 **/
	// always start with this
	if (!gst_element_link (data.convert, data.sink)) {
		g_printerr ("converter and sink could not be linked.\n");
		gst_object_unref (data.pipeline);
		return -1;
	}
	//this is disabled if we use padding cause the external function pad_added_handler handles it
//	if (gst_element_link (data.source, data.sink) != TRUE) {
//		g_printerr ("source and sink could not be linked.\n");
//		gst_object_unref (data.pipeline);
//		return -1;
//	}

	/**
	 * 7) Modify the source's properties
	 * Properties are read from with g_object_get() and written to with g_object_set()
	 **/
	g_object_set (data.source, "pattern", 0, NULL);
	// as an example Set the URI to play
	g_object_set (data.source, "uri", "https://www.freedesktop.org/software/gstreamer-sdk/data/media/sintel_trailer-480p.webm", NULL);

	/** 8) Connect to the pad-added signal
	 * this is needed if there is a delay between the input stream and the data converted form the codex
	 * this calls the external functions that handles the data
	 **/
	g_signal_connect (data.source, "pad-added", G_CALLBACK (pad_added_handler), &data);

	/**
	 * 9) Start playing
	 **/
	ret = gst_element_set_state (data.pipeline, GST_STATE_PLAYING);

	/**
	 * 10) Check if there is an error playing
	 **/
	if (ret == GST_STATE_CHANGE_FAILURE) {
		g_printerr ("Unable to set the pipeline to the playing state.\n");
		gst_object_unref (data.pipeline);
		return -1;
	}


	/**
	 * 11) Listen to the bus
	 **/
	bus = gst_element_get_bus (data.pipeline);
	do {
		msg = gst_bus_timed_pop_filtered (bus, 100 * GST_MSECOND,
		                                  GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
		/**
		 * Parse message
		 * */
		if (msg != NULL) {
			// send the messages to the eternal function handle_message
			handle_message (&data, msg);
		} else {
			/* We got no message, this means the timeout expired */
			if (data.playing) {
				gint64 current = -1;

				/* Query the current position of the stream */
				if (!gst_element_query_position (data.playbin, GST_FORMAT_TIME, &current)) {
					g_printerr ("Could not query current position.\n");
				}

				/* If we didn't know it yet, query the stream duration */
				if (!GST_CLOCK_TIME_IS_VALID (data.duration)) {
					if (!gst_element_query_duration (data.playbin, GST_FORMAT_TIME, &data.duration)) {
						g_printerr ("Could not query current duration.\n");
					}
				}

				/* Print current position and total duration */
				g_print ("Position %" GST_TIME_FORMAT " / %" GST_TIME_FORMAT "\r",
				         GST_TIME_ARGS (current), GST_TIME_ARGS (data.duration));

				/* If seeking is enabled, we have not done it yet, and the time is right, seek */
				if (data.seek_enabled && !data.seek_done && current > 10 * GST_SECOND) {
					g_print ("\nReached 10s, performing seek...\n");
					gst_element_seek_simple (data.playbin, GST_FORMAT_TIME,
					                         GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, 30 * GST_SECOND);
					data.seek_done = TRUE;
				}
			}
		}
	} while (!data.terminate);

	/**
	 * 13) Free resources
	 **/
	gst_object_unref (bus);
	gst_element_set_state (data.pipeline, GST_STATE_NULL);
	gst_object_unref (data.pipeline);
	return 0;
}


