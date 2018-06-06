plugin_LTLIBRARIES = libgsttuio.la

libgsttuio_la_SOURCES = blob_detector.c image_utils.c gstblobstotuio.c
if HAVE_MMX
libgsttuio_la_SOURCES += image_utils_mmx.c
endif
if HAVE_ARM_NEON
libgsttuio_la_SOURCES += image_utils_neon.c
endif
if HAVE_ARM_IWMMXT
libgsttuio_la_SOURCES += image_utils_iwmmxt.c
endif

libgsttuio_la_CFLAGS = $(GST_CFLAGS) $(LIBLO_CFLAGS) -O3
if HAVE_MMX
libgsttuio_la_CFLAGS += $(MMX_CFLAGS)
endif
if HAVE_ARM_NEON
libgsttuio_la_CFLAGS += $(ARM_NEON_CFLAGS)
endif
if HAVE_ARM_IWMMXT
libgsttuio_la_CFLAGS += $(ARM_WMMX_CFLAGS)
endif
libgsttuio_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(GSTCTRL_LIBS) $(LIBLO_LIBS)
libgsttuio_la_LDFLAGS = -no-undefined $(GST_PLUGIN_LDFLAGS)
libgsttuio_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstblobstotuio.h
