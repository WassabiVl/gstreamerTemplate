INCLUDES = $(GTK_CFLAGS) $(GST_CFLAGS) $(GSTPB_BASE_CFLAGS) $(GSTPB_BASE_CFLAGS) $(GST_INTERFACE_LIBS) $(LIBLO_CFLAGS) $(LIBUNIQUE_CFLAGS) $(X11_CFLAGS) $(XTST_CFLAGS)

AM_CPPFLAGS = \
	      -DWEBCAM_DATADIR=\""$(datadir)/$(PACKAGE)"\" \
	      -DWEBCAM_SYSCONFDIR=\""$(sysconfdir)/$(PACKAGE)"\"


if PLATFORM_WIN32
bin_PROGRAMS = gst-webcam-input
else
bin_PROGRAMS = gst-tuio-setting gst-webcam-input
gst_tuio_setting_LDADD      = @GST_LIBS@ @GST_BASE_LIBS@ @GSTPB_BASE_LIBS@ @GST_INTERFACE_LIBS@ @LIBLO_LIBS@ @GTK_LIBS@
gst_tuio_setting_SOURCES = gst-tuio-setting.c
endif

gst_webcam_input_LDADD      = @GST_LIBS@ @GST_BASE_LIBS@ @GSTPB_BASE_LIBS@ @GST_INTERFACE_LIBS@ @LIBLO_LIBS@ @GTK_LIBS@ @LIBUNIQUE_LIBS@ @XTST_LIBS@ 
if PLATFORM_WIN32
else
gst_webcam_input_LDADD	+= @X11_LIBS@
endif
gst_webcam_input_SOURCES = n-point-cal.c \
			gst-webcam-input-cali.c \
			gst-webcam-input-conf.c \
			gst-webcam-input-gst.c \
			gst-webcam-input-driver.c

gst_webcam_input_LDFLAGS = $(AM_LDFLAGS)

if PLATFORM_WIN32
gst_webcam_input_SOURCES += gst-webcam-input-appl-win32.c \
			    gst-webcam-input-win.c \
			    gst-webcam-input-cpuusage-win32.c
# TODO still console not GUI application ?!
gst_webcam_input_LDFLAGS += -mwindows
else
gst_webcam_input_SOURCES += gst-webcam-input-appl.c \
			    gst-webcam-input-uinput.c \
			    gst-webcam-input-x11.c \
			    gst-webcam-input-cpuusage-linux.c
endif

uidir = $(datadir)/$(PACKAGE)
ui_DATA = $(PACKAGE).ui

confdir = $(sysconfdir)/$(PACKAGE)
conf_DATA = $(PACKAGE).conf

imagedir = $(datadir)/$(PACKAGE)
image_DATA = gst-webcam-input.png

EXTRA_DIST = \
	$(ui_DATA) \
	$(image_DATA) \
	#(conf_DATA)
