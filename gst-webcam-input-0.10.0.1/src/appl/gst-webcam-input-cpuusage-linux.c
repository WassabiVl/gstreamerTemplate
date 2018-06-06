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

#include <sys/time.h>
#include <sys/resource.h>
#include <gst/gstclock.h>
#include <gst/gstutils.h>

static struct rusage prior_usage;
static int cpu_usage_init;
static GstClockTime prior_time;

float
get_current_process_cpu_usage(void)
{
  struct rusage now_usage;
  long int msecs_used, msecs_elapsed;
  GstClockTime t;
  float cpu_used;

  cpu_used = -1;
  getrusage(RUSAGE_SELF, &now_usage);

  t = gst_util_get_timestamp();

  if (cpu_usage_init == 1) {
    msecs_used = (now_usage.ru_utime.tv_sec - prior_usage.ru_utime.tv_sec) * 1000
      + (now_usage.ru_utime.tv_usec - prior_usage.ru_utime.tv_usec) / 1000
      + (now_usage.ru_stime.tv_sec - prior_usage.ru_stime.tv_sec) * 1000
      + (now_usage.ru_stime.tv_usec - prior_usage.ru_stime.tv_usec) / 1000;
    msecs_elapsed = GST_TIME_AS_MSECONDS(t - prior_time);
    if (msecs_elapsed > 0) {
      cpu_used = ((float)msecs_used) / msecs_elapsed * 100;
      if(cpu_used>100)
        cpu_used = 100;
    }
  }
  prior_time = t;
  prior_usage = now_usage;
  cpu_usage_init = 1;

  return cpu_used;
}

void
reset_cpu_usage_counter(void)
{
  cpu_usage_init = 0;
}

