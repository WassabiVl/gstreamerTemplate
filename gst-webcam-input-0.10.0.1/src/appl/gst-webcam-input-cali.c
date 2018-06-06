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
#include <lo/lo.h>
#include <string.h>
#include <gdk/gdkkeysyms.h>
#include <gst/gstclock.h>
#include <gst/gstutils.h>
#include <sys/stat.h>
#include <stdlib.h>
#ifdef G_OS_WIN32
#include <io.h>
#endif
#include <math.h>
#include "gst-webcam-input-gst.h"
#include "gst-webcam-input-conf.h"
#include "gst-webcam-input-cali.h"
#include "gst-webcam-input-driver.h"
#include "gst-webcam-input-cpuusage.h"

#include "n-point-cal.h"

#define MIN_SAMPLES 5
#define MAX_SAMPLES 20

enum {
  TOP_LEFT_CALI = 0,
  TOP_RIGHT_CALI,
  BOTTOM_RIGHT_CALI,
  BOTTOM_LEFT_CALI,
  CALI_OKAY,
};

struct blob_coord {
  gfloat x;
  gfloat y;
};

static GtkStatusIcon *trayicon;
static GtkWidget *cali_window, *drawing_area;

static gint calibration_status;

static struct webcam_input_conf *conf;

static guint tuio_watch_id;
static lo_server *tuio_server;

static gint no_of_frames; /* use to show FPS, and warn user if too low FPS */
static gfloat fps;
static GstClockTime last_fps_update;
static guint timeout_id;
static float cpu_used;

static gint no_of_samples;
static gint next_sample_idx;
struct blob_coord blobsamples[MAX_SAMPLES];
struct blob_coord blobraw[4];
struct blob_coord blobscr[4];
static gint screen_width, screen_height;
static gfloat transform_matrix[6];

static void
shutdownlo()
{
  if (tuio_watch_id)
    g_source_remove(tuio_watch_id);
  tuio_watch_id = 0;

  if (tuio_server)
    lo_server_free(tuio_server);
  tuio_server = NULL;

  if (timeout_id)
    g_source_remove(timeout_id);
  timeout_id = 0;
}

static gint
cb_destroy(GtkWidget *w)
{
  webcam_input_finalize_gst();
  shutdownlo();
  gtk_main_quit();
  return TRUE;
}

static void
cb_size_allocate(GtkWidget *w, GtkAllocation *allocation)
{
  screen_width = allocation->width;
  screen_height = allocation->height;
}

static void
save_calibration()
{
#ifdef G_OS_WIN32
/*  _mkdir(WEBCAM_SYSCONFDIR); */
/* TODO */
  return;
#else
  FILE *fp; 
  mkdir(WEBCAM_SYSCONFDIR, 0755);
  fp = fopen (WEBCAM_SYSCONFDIR G_DIR_SEPARATOR_S "gst-webcam-input.cal", "w");
  if (!fp) {
    g_warning("save to " WEBCAM_SYSCONFDIR G_DIR_SEPARATOR_S "gst-webcam-input.cal failed");
  } else {        
    fprintf (fp, "%f,%f,%f,%f,%f,%f\n", transform_matrix[0],
        transform_matrix[1],
        transform_matrix[2],
        transform_matrix[3],
        transform_matrix[4],
        transform_matrix[5]);
    fclose (fp);
  }
#endif
}

static void
calibrate_tuio_to_screen()
{
  gfloat xscr[4];
  gfloat yscr[4];
  gfloat xblob[4];
  gfloat yblob[4];
  int i;
  int portno;

  for (i=0;i<4;i++) {
    xscr[i] = blobscr[i].x * screen_width;
    yscr[i] = blobscr[i].y * screen_height;
    xblob[i] = blobraw[i].x;
    yblob[i] = blobraw[i].y;
  }
  
  n_point_cal(xscr, yscr, xblob, yblob, 4, transform_matrix);

  /* write to file */
  save_calibration();

  if (conf->matrix)
    g_free(conf->matrix);
  conf->matrix = g_strdup_printf("%f,%f,%f,%f,%f,%f", transform_matrix[0],
            transform_matrix[1],
            transform_matrix[2],
            transform_matrix[3],
            transform_matrix[4],
            transform_matrix[5]);

  /* start the tuio stream again and let input driver to handle the TUIO events */
  shutdownlo();
  portno = webcam_input_driver_initlo(NULL, conf->input_drivername);
  webcam_input_initgst(portno, conf);
}

static gfloat
get_camera_viewport_utilization()
{
  int i;
  int camera_area;
  int utilization_area;
  camera_area = conf->camera_width * conf->camera_height;
  utilization_area = 0;
  for (i = 0; i < 3; i++) {
    utilization_area += blobraw[i].x * blobraw[i+1].y;
    utilization_area -= blobraw[i+1].x * blobraw[i].y;
  }
  utilization_area /= 2;
  return ((gfloat)utilization_area) / camera_area;
}

static void
errorlo(int num, const char *msg, const char *path)
{
  g_error("liblo server error %d in path %s: %s\n", num, path, msg);
}

static int
sort_by_x (const void* a, const void *b)
{
  return (((struct blob_coord *)a)->x > ((struct blob_coord *)b)->x);
}

static int
sort_by_y (const void* a, const void *b)
{
  return (((struct blob_coord *)a)->y > ((struct blob_coord *)b)->y);
}

static void
select_median_cali_point()
{
  int middle;
  gfloat sx, sy;

  middle = no_of_samples / 2;
  qsort (blobsamples, no_of_samples, sizeof(struct blob_coord), sort_by_x);
  if (no_of_samples & 1)
    sx = blobsamples[middle].x;
  else
    sx = (blobsamples[middle-1].x + blobsamples[middle].x) / 2;
  qsort (blobsamples, no_of_samples, sizeof(struct blob_coord), sort_by_y);
  if (no_of_samples & 1)
    sy = blobsamples[middle].y;
  else
    sy = (blobsamples[middle-1].y + blobsamples[middle].y) / 2;

  blobraw[calibration_status].x = sx;
  blobraw[calibration_status].y = sy;
}

static gboolean
update_fps(gpointer data)
{
  gint f;
  GstClockTime t;
  GstClockTime duration;
  float c;

  f = no_of_frames;
  no_of_frames = 0;
  t = last_fps_update;
  last_fps_update = gst_util_get_timestamp();

  duration = last_fps_update - t;

  if (duration > 1000*1000*1000) {
    fps = (((float)f) * (float)GST_SECOND) / duration;
    c = get_current_process_cpu_usage();
    if (c >= 0) {
      cpu_used = c;
    }
    gtk_widget_queue_draw(drawing_area);
  }

  return TRUE;
}

static int
tuio2Dcur_handler(const char *path, const char *types, lo_arg **argv,
                    int argc, void *data, void *user)
{
  gint no_of_alive_blobs;

  if (calibration_status == CALI_OKAY) {
    return 0;
  }

  if (!strcmp((char *) argv[0],"set") && (argc > 5)) {
    blobsamples[next_sample_idx].x = argv[2]->f;
    blobsamples[next_sample_idx].y = argv[3]->f;
    next_sample_idx++;
    next_sample_idx %= MAX_SAMPLES;
    if (no_of_samples < MAX_SAMPLES)
      no_of_samples++;
  } else if (!strcmp((char *) argv[0],"alive") ) {
    no_of_alive_blobs = argc-1;
    if (no_of_alive_blobs == 0) {
      /* we have a blob "up" event */
      if (no_of_samples > MIN_SAMPLES) {
        select_median_cali_point();
        calibration_status++;
        next_sample_idx = no_of_samples = 0;
        /* if (calibration_status == CALI_OKAY)
          calibrate_tuio_to_screen(); */
        gtk_widget_queue_draw(drawing_area);
      } else if (no_of_samples != 0) {
        no_of_samples = 0;
        next_sample_idx = 0;
        gtk_widget_queue_draw(drawing_area);
      }
    }
  } else if (!strcmp((char *) argv[0],"fseq") ) {
    no_of_frames++;
    if (no_of_frames % 2) { /* to save some update screen cpu */
      gtk_widget_queue_draw_area(drawing_area, screen_width/4, screen_height/4,
          screen_width/2, screen_height/2);
    }
  }

  return 0;
}

static gboolean
lo_callback(GIOChannel *source, GIOCondition condition, gpointer data) 
{
  lo_server *s=(lo_server *) data;
  lo_server_recv_noblock(s, 0);
  
  return TRUE;
}

static int
initlo(const char *port)
{
  int lo_fd;
  GIOChannel *ioc;

  shutdownlo();
  tuio_server  = lo_server_new(port, errorlo);

  lo_server_add_method(tuio_server, "/tuio/2Dcur", NULL, tuio2Dcur_handler,
      NULL);

  /* add a timer to update FPS and cpu usage */
  timeout_id = g_timeout_add(1000, update_fps, NULL);
  fps = 0;
  cpu_used = -1;
  last_fps_update = gst_util_get_timestamp();

  /* reset cpu usage counter */
  reset_cpu_usage_counter();

  /* get the file descriptor of the server socket, if supported */
  lo_fd = lo_server_get_socket_fd(tuio_server);

  if (lo_fd < 0) {
    g_error("cannto get lo socket");
  }

#ifdef G_OS_WIN32
  ioc = g_io_channel_win32_new_socket(lo_fd);
#else
  ioc = g_io_channel_unix_new(lo_fd);
#endif
  tuio_watch_id = g_io_add_watch(ioc, (GIOCondition)(G_IO_IN | G_IO_PRI),
    lo_callback, (gpointer) tuio_server);
  g_io_channel_unref(ioc);

  return lo_server_get_port(tuio_server);
}

static gboolean
cb_keypress(GtkWidget *w, GdkEventKey *e)
{
  if ((calibration_status == CALI_OKAY)&&(e->keyval!=GDK_BackSpace)) {
    calibrate_tuio_to_screen();
    gtk_widget_hide(w);
    gtk_status_icon_set_visible(trayicon, TRUE);
    return TRUE;
  }

  switch (e->keyval) {
    case GDK_BackSpace:
      if (calibration_status > 0) {
        if (calibration_status == CALI_OKAY) {
          /* start lo again since it is stopped */
          int portno = initlo(NULL);
          if (conf->matrix)
            g_free(conf->matrix);
          conf->matrix = g_strdup("1,0,0,0,1,0");
          webcam_input_initgst(portno, conf);
        }
        calibration_status--;
        no_of_samples = 0;
        next_sample_idx = 0;
        gtk_widget_queue_draw(drawing_area);
      }
      return TRUE;
    case GDK_Escape:
      /* Cancel the calibration procedure !!! */
      webcam_input_finalize_gst();
      shutdownlo();
      gtk_main_quit();
      return TRUE;
  }  

  return FALSE;
}

static gboolean
cb_expose_event(GtkWidget *widget, GdkEventExpose *event, gpointer data)
{
  int width, height;
  int cross_x, cross_y;
  cairo_t *cr;

  gdk_drawable_get_size(widget->window, &width, &height);

#if 0
  /* clean the background to black */
  gdk_draw_rectangle (widget->window, widget->style->black_gc,
      TRUE, 0, 0, width, height);
#endif

  if ((blobscr[0].x == 0) || (screen_width != width) || (screen_height != height)) {
    screen_width = width;
    screen_height = height;
    calibration_status = TOP_LEFT_CALI;
  }

  cr = gdk_cairo_create (widget->window);

  cairo_rectangle(cr, event->area.x, event->area.y,
      event->area.width, event->area.height);
  cairo_clip(cr);

  cairo_text_extents_t te;
  cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
  cairo_select_font_face (cr, "Sans",
      CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size (cr, 12);

  /* draw the real time blob location and screen utlization in the middle of the screen */
  cairo_rectangle (cr, screen_width/4, screen_height/4, screen_width/2, screen_height/2);
  cairo_set_source_rgb (cr, 0, 0, 0);
  cairo_fill (cr);

  cairo_save (cr);

  cairo_rectangle (cr, screen_width/4, screen_height/4, screen_width/2, screen_height/2);
  cairo_clip(cr);
  cairo_translate(cr, screen_width/4, screen_height/4);
  cairo_scale(cr, screen_width / 2. / conf->camera_width, screen_height / 2. / conf->camera_height);

  if (calibration_status == CALI_OKAY) {
    int i;
    cairo_move_to (cr, blobraw[0].x, blobraw[0].y);
    for (i = 1; i < calibration_status; i++) {
      cairo_line_to (cr, blobraw[i].x, blobraw[i].y);
    }
    cairo_close_path (cr);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_fill(cr);
  } else {
    int i;
    /* save dots to show the current hit point */
    if (calibration_status > 0) {
      for (i = 0; i <= calibration_status-1; i++) {
        cairo_arc (cr, blobraw[i].x, blobraw[i].y, 5. * conf->camera_width * 2 / screen_width, 0, 2 * M_PI);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_fill(cr);
      }
    }
    /* draw current sample dot */
    i = next_sample_idx - 1;
    if (i < 0)
      i += MAX_SAMPLES;
    if (no_of_samples != 0) {
      cairo_arc (cr, blobsamples[i].x, blobsamples[i].y, 20. * conf->camera_width * 2 / screen_width, 0, 2 * M_PI);
      cairo_set_source_rgb(cr, 0.5, 1, 1);
      cairo_fill(cr);
    }
  }

  /* restore the original clip and transformation */
  cairo_restore (cr);

  /* restore the clip */
  cairo_rectangle(cr, event->area.x, event->area.y,
      event->area.width, event->area.height);
  cairo_clip(cr);

  /* draw the instruction */
  if (calibration_status == CALI_OKAY) {
    gchar *c;
    gfloat f;
    f = get_camera_viewport_utilization();
    c = g_strdup_printf("Calibration done utilization: %.0f%%, press any key to start use", f*100);
    cairo_text_extents (cr, c, &te);
    cairo_move_to (cr, width / 2 - te.width / 2 - te.x_bearing,
        height / 8 - te.height / 2 - te.y_bearing);
    cairo_show_text (cr, c);
    g_free(c);
  } else {
    if (fps != 0) {
      gchar *c;
      c = g_strdup_printf("Framerate: %.2f FPS", fps);
      cairo_text_extents (cr, c, &te);
      cairo_move_to (cr, width / 2 - te.width / 2 - te.x_bearing,
          height / 8 - te.height / 2 - te.y_bearing);
      cairo_show_text (cr, c);
      g_free(c);
    }
    if (cpu_used >= 0) {
      gchar *c;
      c = g_strdup_printf("CPU used: %.2f%%", cpu_used);
      cairo_text_extents (cr, c, &te);
      cairo_move_to (cr, width / 2 - te.width / 2 - te.x_bearing,
          height / 12 * 2 - te.height / 2 - te.y_bearing);
      cairo_show_text (cr, c);
      g_free(c);
    }
    {
    cairo_text_extents (cr, "Hit the crosses to calibrate.", &te);
    cairo_move_to (cr, width / 2 - te.width / 2 - te.x_bearing,
        height * 7 / 8 - te.height / 2 - te.y_bearing);
    cairo_show_text (cr, "Hit the crosses to calibrate.");
    }
    {
    cairo_text_extents (cr, "Press escape to exit calibration. Press backspace to back to previous calibration point.", &te);
    cairo_move_to (cr, width / 2 - te.width / 2 - te.x_bearing,
        height * 11 / 12 - te.height / 2 - te.y_bearing);
    cairo_show_text (cr, "Press escape to exit calibration. Press backspace to back to previous calibration point.");
    }

    /* draw cross */
    cross_x = blobscr[calibration_status].x * width;
    cross_y = blobscr[calibration_status].y * height;

    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_move_to (cr, cross_x, cross_y-5);
    cairo_line_to (cr, cross_x, cross_y+5);
    cairo_move_to (cr, cross_x-5, cross_y);
    cairo_line_to (cr, cross_x+5, cross_y);
    cairo_set_line_width(cr, 3);
    cairo_stroke (cr);
  }

  cairo_destroy(cr);

  return FALSE;
}

static void
cb_trayicon_activate(GObject *trayicon, gpointer window)
{
}

static void
cb_trayicon_popup_menu(GtkStatusIcon *status_icon, guint button, guint32 activate_time, gpointer menu)
{
    gtk_menu_popup(GTK_MENU(menu), NULL, NULL, gtk_status_icon_position_menu, status_icon, button, activate_time);
}

static void
cb_trayicon_calibrate(GtkMenuItem *item, gpointer user_data) 
{
  /* start the calibration again */
  webcam_input_start_calibrate();
}

static void
cb_trayicon_settings(GtkMenuItem *item, gpointer user_data) 
{
}

static void
cb_trayicon_about(GtkMenuItem *item, gpointer user_data) 
{
        const gchar *authors[] = {
                "Keith Mok <ek9852@gmail.com>",
                NULL
        };

        gtk_show_about_dialog(NULL, //"version", VERSION,
                "copyright", "Copyright \xc2\xa9 2009-2010 Keith Mok",
                "comments", "A input device using webcam and infrared",
                "authors", authors, NULL);
//                "logo-icon-name", "webcam", NULL);
}

static void
cb_trayicon_exit(GtkMenuItem *item, gpointer user_data) 
{
  webcam_input_finalize_gst();
  shutdownlo();
  gtk_main_quit();
}

static void
create_trayicon()
{
  GtkWidget *menu, *menuitem_cali, *menuitem_settings, *menuitem_about, *menuitem_exit;
  GtkWidget *sep;

#ifdef G_OS_WIN32
  gchar *dir, *iconfile;

  dir = g_win32_get_package_installation_directory_of_module (NULL);
  iconfile = g_build_filename (dir, "share", "gst-webcam-input",
      "gst-webcam-input.png", NULL);
  g_free (dir);

  trayicon = gtk_status_icon_new_from_file (iconfile);

  g_free (iconfile);
#else
  trayicon = gtk_status_icon_new_from_file (WEBCAM_DATADIR G_DIR_SEPARATOR_S "gst-webcam-input.png");
#endif

  menu = gtk_menu_new();
  menuitem_cali = gtk_menu_item_new_with_label ("Calibrate");
  menuitem_settings  = gtk_menu_item_new_with_label ("Settings");
  sep = gtk_separator_menu_item_new ();
  menuitem_about = gtk_menu_item_new_with_label ("About");
  menuitem_exit = gtk_menu_item_new_with_label ("Exit");
  g_signal_connect (G_OBJECT (menuitem_cali), "activate", G_CALLBACK (cb_trayicon_calibrate), NULL);
  g_signal_connect (G_OBJECT (menuitem_settings), "activate", G_CALLBACK (cb_trayicon_settings), NULL);
  g_signal_connect (G_OBJECT (menuitem_about), "activate", G_CALLBACK (cb_trayicon_about), NULL);
  g_signal_connect (G_OBJECT (menuitem_exit), "activate", G_CALLBACK (cb_trayicon_exit), NULL);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem_cali);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem_settings);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), sep);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem_about);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem_exit);
  gtk_widget_show_all (menu);

  /* set tooltip */
  gtk_status_icon_set_tooltip (trayicon, "Webcam to touch event based on blob detection");

  /* connect handlers for mouse events */
  g_signal_connect(GTK_STATUS_ICON (trayicon), "activate", GTK_SIGNAL_FUNC (cb_trayicon_activate), menu);
  g_signal_connect(GTK_STATUS_ICON (trayicon), "popup-menu", GTK_SIGNAL_FUNC (cb_trayicon_popup_menu), menu);
  gtk_status_icon_set_visible(trayicon, FALSE);
}

GtkWidget*
webcam_input_create_calibration_window (struct webcam_input_conf *c)
{
  int portno;
  create_trayicon();

  blobscr[TOP_LEFT_CALI].x = 0.1;
  blobscr[TOP_LEFT_CALI].y = 0.1;
  blobscr[TOP_RIGHT_CALI].x = 1 - 0.1;
  blobscr[TOP_RIGHT_CALI].y = 0.1;
  blobscr[BOTTOM_RIGHT_CALI].x = 1 - 0.1;
  blobscr[BOTTOM_RIGHT_CALI].y = 1 - 0.1;
  blobscr[BOTTOM_LEFT_CALI].x = 0.1;
  blobscr[BOTTOM_LEFT_CALI].y = 1 - 0.1;

  /* create the calibration screen */
  cali_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

  /* title */
  gtk_window_set_title (GTK_WINDOW (cali_window), "webcam to touch screen calibration");

  drawing_area = gtk_drawing_area_new();
  g_signal_connect(drawing_area, "expose_event",
      G_CALLBACK(cb_expose_event), NULL);

  gtk_container_add (GTK_CONTAINER (cali_window), drawing_area);

  g_signal_connect (G_OBJECT (cali_window), "destroy",
      G_CALLBACK (cb_destroy), NULL);
  g_signal_connect (G_OBJECT (cali_window), "size-allocate",
      G_CALLBACK (cb_size_allocate), NULL);
  g_signal_connect (G_OBJECT (cali_window),
      "key-press-event",
      G_CALLBACK (cb_keypress), NULL);

  /* init osc */
  portno = initlo(NULL);

  gtk_window_fullscreen(GTK_WINDOW(cali_window));
  gtk_widget_show_all (cali_window);
  calibration_status = TOP_LEFT_CALI;

  conf = c;

  /* start the tuio stream */
  if (conf->matrix)
    g_free(conf->matrix);
  conf->matrix = g_strdup("1,0,0,0,1,0");
  webcam_input_initgst(portno, conf);

  return cali_window;
}

void
webcam_input_start_calibrate()
{
  int portno;
  no_of_samples = 0;
  next_sample_idx = 0;
  calibration_status = TOP_LEFT_CALI;
  gtk_widget_show(cali_window);
  gtk_widget_queue_draw(drawing_area);

  /* start the tuio stream again, it will cancel the previous stream */
  webcam_input_driver_shutdownlo();
  portno = initlo(NULL);
  if (conf->matrix)
    g_free(conf->matrix);
  conf->matrix = g_strdup("1,0,0,0,1,0");
  webcam_input_initgst(portno, conf);
}

