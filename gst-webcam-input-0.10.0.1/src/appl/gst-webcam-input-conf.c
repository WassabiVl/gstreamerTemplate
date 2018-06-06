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
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <ctype.h>
#include <unistd.h>
#include "gst-webcam-input-conf.h"

#define READ_CONFIG_BUF_SIZE 80

static int
read_str(const char *line, void *arg)
{
  char **dest = arg;
  char *p;

  if (*dest) {
    g_free(*dest);
    *dest = NULL;
  }

  if (line && (*line == '"'))
    line++;
  *dest = g_strdup(line);

  p = *dest;

  if (dest && *dest && (p[strlen(p)-1] == '"'))
    p[strlen(p)-1] = 0;

  return !!(*dest);
}

static int
read_int(const char *line, void *arg)
{
  gint *dest = arg;
  char *endptr;
  *dest = strtoul(line, &endptr, 0);
  return endptr[0] == '\0';
}

struct config_keyword {
  const char *keyword;
  int (* const handler)(const char *line, void *var);
  int var_offset;
  const char *def;
};

static const struct config_keyword keywords[] = {
  /* keyword      handler   variable address              default */
  {"camera_width",             read_int,  offsetof(struct webcam_input_conf, camera_width),       "176"},
  {"camera_height",            read_int,  offsetof(struct webcam_input_conf, camera_height),      "144"},
  {"v4l2_devname",             read_str,  offsetof(struct webcam_input_conf, v4l2_devname),       NULL},
#ifdef G_OS_WIN32
  {"input_drivername",         read_str,  offsetof(struct webcam_input_conf, input_drivername),       "win32 single touch driver"},
#else
  {"input_drivername",         read_str,  offsetof(struct webcam_input_conf, input_drivername),       "X11 single touch driver"},
//  {"input_drivername",         read_str,  offsetof(struct webcam_input_conf, input_drivername),       "uinput single touch driver"},
#endif
  {"uinput_devname",           read_str,  offsetof(struct webcam_input_conf, uinput_devname),     NULL},
  {"learn_background_counter", read_int,  offsetof(struct webcam_input_conf, learn_background_counter), "15"},
  {"threshold",                read_int,  offsetof(struct webcam_input_conf, threshold),          "25"},
  {"smooth",                   read_int,  offsetof(struct webcam_input_conf, smooth),             "2"},
  {"highpass_blur",            read_int,  offsetof(struct webcam_input_conf, highpass_blur),      "16"},
  {"highpass_noise",           read_int,  offsetof(struct webcam_input_conf, highpass_noise),     "4"},
  {"amplify",                  read_int,  offsetof(struct webcam_input_conf, amplify),            "16"},
  {"surface_min",              read_int,  offsetof(struct webcam_input_conf, surface_min),        "20"},
  {"surface_max",              read_int,  offsetof(struct webcam_input_conf, surface_max),        "500"},
  {"distance_max",             read_int,  offsetof(struct webcam_input_conf, distance_max),       "20"},
//  {"matrix",                   read_int,  ,         ""},
  {NULL,            NULL,     0,                         NULL}
};

static int
read_config(const char *file, struct webcam_input_conf *conf)
{
  FILE *in;
  char buffer[READ_CONFIG_BUF_SIZE], *token, *line;
  unsigned char *pconf = (unsigned char *)conf;
  int i, lm = 0;

  for (i = 0; keywords[i].keyword; i++)
    if (keywords[i].def)
      keywords[i].handler(keywords[i].def, pconf + keywords[i].var_offset);

  if (!(in = fopen(file, "r"))) {
    g_warning("unable to open config file: %s", file);
    return -1;
  }

  g_message("Using config file: %s", file);

  while (fgets(buffer, READ_CONFIG_BUF_SIZE, in)) {

    lm++;
    if (strchr(buffer, '\n')) *(strchr(buffer, '\n')) = '\0';
    if (strchr(buffer, '#')) *(strchr(buffer, '#')) = '\0';

    if (!(token = strtok(buffer, " \t"))) continue;
    if (!(line = strtok(NULL, ""))) continue;

    /* eat leading whitespace */
    line = line + strspn(line, " \t=");
    /* eat trailing whitespace */
    for (i = strlen(line); i > 0 && isspace(line[i - 1]); i--);
    line[i] = '\0';

    for (i = 0; keywords[i].keyword; i++)
      if (!strcasecmp(token, keywords[i].keyword))
        if (!keywords[i].handler(line, pconf + keywords[i].var_offset)) {
          g_warning("Failure parsing line %d of %s", lm, file);
          /* reset back to the default value */
          keywords[i].handler(keywords[i].def, pconf + keywords[i].var_offset);
        }
  }
  fclose(in);
  return 0;
}


struct webcam_input_conf *
webcam_input_load_conf(void)
{
  struct webcam_input_conf *conf;
  char *path;
  char *config_file_p, *conffile = NULL;

  conf = g_malloc0(sizeof(struct webcam_input_conf));

  /* Check in user home directory for the conf file first, then check for /etc */
  path = g_build_filename(g_get_home_dir(), ".gst-webcam-input", "gst-webcam-input.conf", NULL);
  if (!access(path, R_OK))
    config_file_p = path;
  else {
#ifdef G_OS_WIN32
    gchar *dir;

    dir = g_win32_get_package_installation_directory_of_module (NULL);
    conffile = g_build_filename (dir, "etc", "gst-webcam-input",
        "gst-webcam-input.conf", NULL);
    g_free (dir);

    config_file_p = conffile;
#else
    conffile = g_strdup(WEBCAM_SYSCONFDIR G_DIR_SEPARATOR_S "gst-webcam-input.conf");
    config_file_p = conffile;
#endif
  }

  read_config(config_file_p, conf);
  
  g_free(path);
  if (conffile)
    g_free(conffile);

  conf->matrix = g_strdup("1,0,0,0,1,0");

  return conf;
}

void
webcam_input_finalize_conf(struct webcam_input_conf *conf)
{
  if(conf->v4l2_devname)
    g_free(conf->v4l2_devname);
  if(conf->input_drivername)
    g_free(conf->input_drivername);
  if(conf->uinput_devname)
    g_free(conf->uinput_devname);
  if(conf->matrix)
    g_free(conf->matrix);

}

