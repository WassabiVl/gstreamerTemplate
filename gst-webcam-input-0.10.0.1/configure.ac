AC_INIT

dnl versions of gstreamer and plugins-base
GST_MAJORMINOR=0.10
GST_REQUIRED=0.10.0
GSTPB_REQUIRED=0.10.0

dnl fill in your package name and version here
dnl the fourth (nano) number should be 0 for a release, 1 for CVS,
dnl and 2... for a prerelease

dnl when going to/from release please set the nano correctly !
dnl releases only do Wall, cvs and prerelease does Werror too
AS_VERSION(gst-webcam-input, GST_PLUGIN_VERSION, 0, 10, 0, 1,
    GST_PLUGIN_CVS="no", GST_PLUGIN_CVS="yes")

dnl AM_MAINTAINER_MODE provides the option to enable maintainer mode
AM_MAINTAINER_MODE

AM_INIT_AUTOMAKE($PACKAGE, $VERSION)

dnl sets build variables and the same with _alias
AC_CANONICAL_HOST

case "${host}" in
  *-*-cygwin* | *-*-mingw32* )
    platform_win32=yes
    DEFAULT_INPUT_DRIVER="win32 single touch driver"
    ;;
  *)
    platform_win32=no
    DEFAULT_INPUT_DRIVER="X11 single touch driver"
    ;;
esac

AM_CONDITIONAL(PLATFORM_WIN32, test "x$platform_win32" = "xyes")
AC_SUBST(DEFAULT_INPUT_DRIVER)

# copied from vlc configure.ac for mmx detection
AC_ARG_ENABLE(mmx,
[  --disable-mmx           disable MMX optimizations (default auto)],,[
  case "${host_cpu}" in
    i?86|x86_64)
      enable_mmx="yes"
      ;;
    *)
      enable_mmx="no"
      ;;
  esac
])
have_mmx="no"
have_mmxext="no"
CFLAGS_save="${CFLAGS}"
AS_IF([test "${enable_mmx}" != "no"], [
  AC_CACHE_CHECK([if $CC groks MMX intrinsics],
    [ac_cv_c_mmx_intrinsics],
    [CFLAGS="${CFLAGS_save} -O -mmmx"
     AC_TRY_COMPILE([#include <mmintrin.h>
                     #include <stdint.h>
                     uint64_t frobzor;],
                    [__m64 a, b, c;
                     a = b = c = (__m64)frobzor;
                     a = _mm_slli_pi16(a, 3);
                     a = _mm_adds_pi16(a, b);
                     c = _mm_srli_pi16(c, 8);
                     c = _mm_slli_pi16(c, 3);
                     b = _mm_adds_pi16(b, c);
                     a = _mm_unpacklo_pi8(a, b);
                     frobzor = (uint64_t)a;],
                    [ac_cv_c_mmx_intrinsics=yes],
                    [ac_cv_c_mmx_intrinsics=no])])
  AS_IF([test "${ac_cv_c_mmx_intrinsics}" != "no"], [
    AC_DEFINE(HAVE_MMX_INTRINSICS, 1,
              [Define to 1 if MMX intrinsics are available.])
    MMX_CFLAGS="-mmmx"
  ])

  AC_CACHE_CHECK([if $CC groks MMX inline assembly],
    [ac_cv_mmx_inline],
    [CFLAGS="${CFLAGS_save}"
     AC_TRY_COMPILE(,[void *p;asm volatile("packuswb %%mm1,%%mm2"::"r"(p));],
                    ac_cv_mmx_inline=yes, ac_cv_mmx_inline=no)])
  AS_IF([test "${ac_cv_mmx_inline}" != "no"], [
    AC_DEFINE(CAN_COMPILE_MMX, 1,
              [Define to 1 inline MMX assembly is available.])
    have_mmx="yes"
  ])

  AC_CACHE_CHECK([if $CC groks MMX EXT inline assembly],
    [ac_cv_mmxext_inline],
    [CFLAGS="${CFLAGS_save}"
     AC_TRY_COMPILE(,[void *p;asm volatile("maskmovq %%mm1,%%mm2"::"r"(p));],
                    ac_cv_mmxext_inline=yes, ac_cv_mmxext_inline=no)])
  AS_IF([test "${ac_cv_mmxext_inline}" != "no"], [
    AC_DEFINE(CAN_COMPILE_MMXEXT, 1,
              [Define to 1 if MMX EXT inline assembly is available.])
    have_mmxext="yes"
  ])
])
AC_SUBST(MMX_CFLAGS)
AM_CONDITIONAL([HAVE_MMX], [test "${have_mmx}" = "yes"])
AM_CONDITIONAL([HAVE_MMXEXT], [test "${have_mmxext}" = "yes"])

dnl check for ARM iwmmxt support
AC_ARG_ENABLE(iwmmxt,
[  --disable-iwmmxt          disable IWMMXT optimizations (default auto)],, [
  AS_IF([test "${host_cpu}" = "arm"], [enable_iwmmxt="yes"] ,[enable_iwmmxt="no"])
])
AS_IF([test "${enable_iwmmxt}" != "no"], [
  AC_CACHE_CHECK([if $CC groks IWMMXT inline assembly], [ac_cv_iwmmxt_inline], [
    CFLAGS="${CFLAGS_save} $(ARM_CFLAGS)"
    AC_COMPILE_IFELSE([
      AC_LANG_PROGRAM(,[[asm volatile("wmul wr0, wr0, wr0":::);]])
    ], [
      ac_cv_iwmmxt_inline="yes"
    ], [
      ac_cv_iwmmxt_inline="no"
    ])
    CFLAGS="${CFLAGS_save}"
  ])
], [
  ac_cv_iwmmxt_inline="no"
])
AC_SUBST(ARM_IWMMXT_CFLAGS)
AM_CONDITIONAL(HAVE_ARM_IWMMXT, [test "${ac_cv_iwmmxt_inline}" != "no"])

dnl check for ARM neon support
AC_ARG_ENABLE(neon,
[  --disable-neon          disable NEON optimizations (default auto)],, [
  AS_IF([test "${host_cpu}" = "arm"], [enable_neon="yes"] ,[enable_neon="no"])
])
AS_IF([test "${enable_neon}" != "no"], [
  AC_CACHE_CHECK([if $CC groks NEON inline assembly], [ac_cv_neon_inline], [
    CFLAGS="${CFLAGS_save} $(ARM_CFLAGS)"
    AC_COMPILE_IFELSE([
      AC_LANG_PROGRAM(,[[asm volatile("vqmovun.s64 d0, q1":::"d0");]])
    ], [
      ac_cv_neon_inline="yes"
    ], [
      ac_cv_neon_inline="no"
    ])
    CFLAGS="${CFLAGS_save}"
  ])
], [
  ac_cv_neon_inline="no"
])
AC_SUBST(ARM_NEON_CFLAGS)
AM_CONDITIONAL(HAVE_ARM_NEON, [test "${ac_cv_neon_inline}" != "no"])

dnl make aclocal work in maintainer mode
AC_SUBST(ACLOCAL_AMFLAGS, "-I m4")

AM_CONFIG_HEADER(config.h)

dnl check for tools
AC_PROG_CC
AC_PROG_LIBTOOL


dnl decide on error flags
AS_COMPILER_FLAG(-Wall, GST_WALL="yes", GST_WALL="no")

if test "x$GST_WALL" = "xyes"; then
   GST_ERROR="$GST_ERROR -Wall"

   if test "x$GST_PLUGIN_CVS" = "xyes"; then
     AS_COMPILER_FLAG(-Werror,GST_ERROR="$GST_ERROR -Werror",GST_ERROR="$GST_ERROR")
   fi
fi

dnl Check for pkgconfig first
AC_CHECK_PROG(HAVE_PKGCONFIG, pkg-config, yes, no)

dnl Give error and exit if we don't have pkgconfig
if test "x$HAVE_PKGCONFIG" = "xno"; then
  AC_MSG_ERROR(you need to have pkgconfig installed !)
fi

dnl Now we're ready to ask for gstreamer libs and cflags
dnl And we can also ask for the right version of gstreamer


PKG_CHECK_MODULES(GST, \
  gstreamer-$GST_MAJORMINOR >= $GST_REQUIRED,
  HAVE_GST=yes,HAVE_GST=no)

dnl Give error and exit if we don't have gstreamer
if test "x$HAVE_GST" = "xno"; then
  AC_MSG_ERROR(you need gstreamer development packages installed !)
fi

dnl append GST_ERROR cflags to GST_CFLAGS
GST_CFLAGS="$GST_CFLAGS $GST_ERROR"

dnl make GST_CFLAGS and GST_LIBS available
AC_SUBST(GST_CFLAGS)
AC_SUBST(GST_LIBS)

dnl make GST_MAJORMINOR available in Makefile.am
AC_SUBST(GST_MAJORMINOR)

dnl Check for GTK for calibration program use
PKG_CHECK_MODULES(GTK, "gtk+-2.0", HAVE_GTK=yes, HAVE_GTK=no)

dnl Give error and exit if we don't have gtk
if test "x$HAVE_GTK" = "xno"; then
  AC_MSG_ERROR(you need GTK development packages installed !)
fi

dnl make GTK_CFLAGS and GTK_LIBS available
AC_SUBST(GTK_CFLAGS)
AC_SUBST(GTK_LIBS)

if test "$platform_win32" = no; then
  dnl Check for xextproto for calibration program use
  PKG_CHECK_MODULES(XTST, "xtst", HAVE_XTST=yes, HAVE_XTST=no)
  
  dnl Give error and exit if we don't have xtest
  if test "x$HAVE_XTST" = "xno"; then
    AC_MSG_ERROR(you need XTST development packages installed !)
  fi
  
  dnl make XTST_CFLAGS and XTST_LIBS available
  AC_SUBST(XTST_CFLAGS)
  AC_SUBST(XTST_LIBS)
  
  dnl Check for X11 for calibration program use
  PKG_CHECK_MODULES(X11, "x11", HAVE_X11=yes, HAVE_X11=no)
  
  dnl Give error and exit if we don't have x11
  if test "x$HAVE_X11" = "xno"; then
    AC_MSG_ERROR(you need X11 development packages installed !)
  fi
  
  dnl make X11_CFLAGS and X11_LIBS available
  AC_SUBST(X11_CFLAGS)
  AC_SUBST(X11_LIBS)
  
  dnl Check for libunique for calibration program use
  PKG_CHECK_MODULES(LIBUNIQUE, unique-1.0, HAVE_LIBUNIQUE=yes, HAVE_LIBUNIQUE=no)
  
  dnl Give error and exit if we don't have libunique
  if test "x$HAVE_LIBUNIQUE" = "xno"; then
    AC_MSG_ERROR(you need libunique development packages installed !)
  fi
  
  dnl make LIBUNIQUE_CFLAGS and LIBUNIQUE_LIBS available
  AC_SUBST(LIBUNIQUE_CFLAGS)
  AC_SUBST(LIBUNIQUE_LIBS)
fi


dnl Check for liblo for TUIO communcation use
PKG_CHECK_MODULES(LIBLO, liblo, HAVE_LIBLO=yes, HAVE_LIBLO=no)

dnl Give error and exit if we don't have liblo 
if test "x$HAVE_LIBLO" = "xno"; then
  AC_MSG_ERROR(you need liblo development packages installed !)
fi

dnl make LIBLO_CFLAGS and LIBLO_LIBS available
AC_SUBST(LIBLO_CFLAGS)
AC_SUBST(LIBLO_LIBS)

dnl If we need them, we can also use the base class libraries
PKG_CHECK_MODULES(GST_BASE, gstreamer-base-$GST_MAJORMINOR >= $GST_REQUIRED,
                  HAVE_GST_BASE=yes, HAVE_GST_BASE=no)

dnl Give a warning if we don't have gstreamer libs
dnl you can turn this into an error if you need them
if test "x$HAVE_GST_BASE" = "xno"; then
  AC_MSG_NOTICE(no GStreamer base class libraries found (gstreamer-base-$GST_MAJORMINOR))
fi

dnl make _CFLAGS and _LIBS available
AC_SUBST(GST_BASE_CFLAGS)
AC_SUBST(GST_BASE_LIBS)

dnl If we need them, we can also use the gstreamer-plugins-base libraries
PKG_CHECK_MODULES(GSTPB_BASE,
                  gstreamer-plugins-base-$GST_MAJORMINOR >= $GSTPB_REQUIRED,
                  HAVE_GSTPB_BASE=yes, HAVE_GSTPB_BASE=no)

dnl Give a warning if we don't have gstreamer libs
dnl you can turn this into an error if you need them
if test "x$HAVE_GSTPB_BASE" = "xno"; then
  AC_MSG_NOTICE(no GStreamer Plugins Base libraries found (gstreamer-plugins-base-$GST_MAJORMINOR))
fi

dnl make _CFLAGS and _LIBS available
AC_SUBST(GSTPB_BASE_CFLAGS)
AC_SUBST(GSTPB_BASE_LIBS)

dnl If we need them, we can also use the gstreamer-interfaces libraries
PKG_CHECK_MODULES(GST_INTERFACE,
                  gstreamer-interfaces-$GST_MAJORMINOR >= $GST_REQUIRED,
                  HAVE_GST_INTERFACE=yes, HAVE_GST_INTERFACE=no)

dnl Give a warning if we don't have gstreamer interfaces
dnl you can turn this into an error if you need them
if test "x$HAVE_GST_INTERFACE" = "xno"; then
  AC_MSG_NOTICE(no GStreamer Interface libraries found (gstreamer-interface-$GST_MAJORMINOR))
fi

dnl make _CFLAGS and _LIBS available
AC_SUBST(GST_INTERFACE_CFLAGS)
AC_SUBST(GST_INTERFACE_LIBS)

dnl If we need them, we can also use the gstreamer-controller libraries
PKG_CHECK_MODULES(GSTCTRL,
                  gstreamer-controller-$GST_MAJORMINOR >= $GSTPB_REQUIRED,
                  HAVE_GSTCTRL=yes, HAVE_GSTCTRL=no)

dnl Give a warning if we don't have gstreamer-controller
dnl you can turn this into an error if you need them
if test "x$HAVE_GSTCTRL" = "xno"; then
  AC_MSG_NOTICE(no GStreamer Controller libraries found (gstreamer-controller-$GST_MAJORMINOR))
fi

dnl make _CFLAGS and _LIBS available
AC_SUBST(GSTCTRL_CFLAGS)
AC_SUBST(GSTCTRL_LIBS)

dnl set the plugindir where plugins should be installed
if test "x${prefix}" = "x$HOME"; then
  plugindir="$HOME/.gstreamer-$GST_MAJORMINOR/plugins"
else
  plugindir="\$(libdir)/gstreamer-$GST_MAJORMINOR"
fi
AC_SUBST(plugindir)

dnl set proper LDFLAGS for plugins
GST_PLUGIN_LDFLAGS='-module -avoid-version -export-symbols-regex [_]*\(gst_\|Gst\|GST_\).*'
AC_SUBST(GST_PLUGIN_LDFLAGS)

AC_OUTPUT(Makefile m4/Makefile src/Makefile src/gst-plugin/Makefile src/appl/Makefile src/appl/gst-webcam-input.conf)

