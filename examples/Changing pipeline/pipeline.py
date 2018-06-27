#!/usr/bin/env python

import sys
import time
import gi
gi.require_version('Gst', '1.0')
from gi.repository import GObject, Gst

port = 5000
pipeline = None
start_time = None;

def bus_call(bus, message, loop):
    t = message.type
    if t == Gst.MessageType.EOS:
        sys.stout.write("End-of-stream\n")
        loop.quit()
    elif t == Gst.MessageType.ERROR:
        err, debug = message.parse_error()
        sys.stderr.write("Error: %s: %s\n" % (err, debug))
        loop.quit()
    else:
        current_time = time.time()

        global start_time
        dif = current_time - start_time

        if (dif > 10):
            global port
            port += 1;
            print port, " New port\n"
            loop.quit()
    return True

def add_source(first_link, port):
    src = Gst.ElementFactory.make ("udpsrc", "source")
    src.set_property("port", port)
    src.set_property("caps", Gst.Caps.from_string("application/x-rtp"))
    rtpvp8depay = Gst.ElementFactory.make("rtpvp8depay", "depay")
    vp8dec = Gst.ElementFactory.make ("vp8dec", "decoder")
    alpha = Gst.ElementFactory.make ("alpha", "alpha")
    alpha.set_property("method", "green")
    pipeline.add (src)
    pipeline.add (rtpvp8depay)
    pipeline.add (vp8dec)
    pipeline.add (alpha)

    if first_link is not None:
        first_link.link(src)

    src.link(rtpvp8depay)
    rtpvp8depay.link(vp8dec)
    vp8dec.link(alpha)
    return alpha

def initialise_pipeline():
    global start_time
    start_time = time.time();
    global pipeline
    pipeline = Gst.Pipeline.new("pipeline")
    global port
    alpha = add_source(None, port);
    videomixer = Gst.ElementFactory.make ("videomixer", "mixer");
    videoconvert = Gst.ElementFactory.make ("videoconvert", "converter");
    autovideosink = Gst.ElementFactory.make ("autovideosink", "sink");
    pipeline.add (videomixer)
    pipeline.add (videoconvert)
    pipeline.add (autovideosink)
    alpha.link(videomixer)
    videomixer.link(videoconvert)
    videoconvert.link(autovideosink)

def main(args):
    GObject.threads_init()
    Gst.init(None)

    global port

    while (port <= 5002):
        initialise_pipeline()

        # create and event loop and feed gstreamer bus mesages to it
        loop = GObject.MainLoop()

        bus = pipeline.get_bus()
        bus.add_signal_watch()
        bus.connect ("message", bus_call, loop)

        # start play back and listed to events
        pipeline.set_state(Gst.State.PLAYING)

        try:
            loop.run()
        except:
            pass

        # cleanup
        pipeline.set_state(Gst.State.NULL)

if __name__ == '__main__':
    sys.exit(main(sys.argv))
