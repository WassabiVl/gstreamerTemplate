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

#include <windows.h>

static int cpu_usage_init;
static FILETIME prior_sys_kernel;
static FILETIME prior_sys_user;
static FILETIME prior_proc_kernel;
static FILETIME prior_proc_user;

static ULONGLONG
subtract_time(const FILETIME *fta, const FILETIME *ftb)
{
  ULARGE_INTEGER a, b;
  a.LowPart = fta->dwLowDateTime;
  a.HighPart = fta->dwHighDateTime;

  b.LowPart = ftb->dwLowDateTime;
  b.HighPart = ftb->dwHighDateTime;

  return a.QuadPart - b.QuadPart;
}

float
get_current_process_cpu_usage(void)
{
  FILETIME sys_idle, sys_kernel, sys_user;
  FILETIME proc_creation, proc_exit, proc_kernel, proc_user;
  float cpu_used;

  cpu_used = -1;

  GetSystemTimes(&sys_idle, &sys_kernel, &sys_user);
  GetProcessTimes(GetCurrentProcess(), &proc_creation, &proc_exit, &proc_kernel, &proc_user);

	if (cpu_usage_init == 1) {
    ULONGLONG total_sys = subtract_time(&sys_kernel, &prior_sys_kernel) + subtract_time(&sys_user, &prior_sys_user);
    ULONGLONG total_proc = subtract_time(&proc_kernel, &prior_proc_kernel) + subtract_time(&proc_user, &prior_proc_user);

    if (total_sys > 0) {
      cpu_used = ((float)total_proc)*100/total_sys;
    }
  }

  prior_sys_kernel = sys_kernel;
  prior_sys_user = sys_user;
  prior_proc_kernel = proc_kernel;
  prior_proc_user = proc_user;

  return cpu_used;
}
	
void
reset_cpu_usage_counter(void)
{
  cpu_usage_init = 0;
}

