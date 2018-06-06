/*
 *  gst-tuio - Gstreamer to tuio computer vision plugin
 *
 *  Copyright (C) 2010 Keith Mok <ek9852@gmail.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "blob_detector.h"

const struct _Zone default_zone = {
  0, 0,
  G_MAXINT, 0,
  G_MAXINT, 0,
  0,
  FALSE
};

#define DEFAULT_ZONE_ARRAY_SIZE 100

static gint
quick_union_root(GArray *zoneid, gint i)
{
  while (i != g_array_index(zoneid, gint, i)) {
    gint *id_i;
    id_i = &g_array_index(zoneid, gint, i);
    *id_i = g_array_index(zoneid, gint, g_array_index(zoneid, gint, i));
    i = g_array_index(zoneid, gint, i);
  }
  return i;
}

static void
quick_union_unite(GArray *zoneid, gint p, gint q)
{
  gint *id_i;
  /* TODO use weighted to upgrade performance */
  gint i = quick_union_root(zoneid, p);
  gint j = quick_union_root(zoneid, q);

  if(i == j) return;

  id_i = &g_array_index(zoneid, gint, i);
  *id_i = j;
}

static void
create_zones(GArray **ret_zoneid, GArray **ret_zonearray, int arraysize)
{
  gint i;
  GArray *zoneid= g_array_sized_new(FALSE, FALSE, sizeof(gint), arraysize);
  GArray *zonearray = g_array_sized_new(FALSE, FALSE, sizeof(Zone), arraysize);
  
  /* Initialize the zonepointer to zone */
  for(i=0; i<arraysize; i++) {
    g_array_append_val(zoneid, i);
  }
  *ret_zoneid = zoneid;
  *ret_zonearray = zonearray;
}

static void
join_zones(GArray *zoneid, gint p, gint q)
{
  /* id is one based */
  quick_union_unite(zoneid, p-1, q-1);
}

static void
update_zone(GArray *zoneid, GArray *zonearray, gint x, gint y, gint id)
{
  Zone *zone;

  /* id is one based */
  id--;

  if(zoneid->len <= id) {
    g_array_append_val(zoneid, id);
  }
  if(zonearray->len <= id) {
    g_array_append_val(zonearray, default_zone);
  }
  zone = &g_array_index(zonearray, Zone, id);
  zone->surface_size++;

  zone->total_x += x;
  zone->total_y += y;
  if(x<zone->xstart)
    zone->xstart = x;
  if(y<zone->ystart)
    zone->ystart = y;
  if(x>zone->xend)
    zone->xend = x;
  if(y>zone->yend)
    zone->yend = y;
}

static void
zone_root_count(GArray *zoneid, GArray *zonearray)
{
  gint i;
  gint id;
  Zone *zone_parent;
  Zone *zone_child;
  for(i=0; i<zonearray->len; i++) {
    id = quick_union_root(zoneid, i);
    if(id != i) {
      zone_parent = &g_array_index(zonearray, Zone, id);
      zone_child = &g_array_index(zonearray, Zone, i);
      if(zone_parent->xstart > zone_child->xstart)
        zone_parent->xstart = zone_child->xstart;
      if(zone_parent->ystart > zone_child->ystart)
        zone_parent->ystart = zone_child->ystart;
      if(zone_parent->xend < zone_child->xend)
        zone_parent->xend = zone_child->xend;
      if(zone_parent->yend < zone_child->yend)
        zone_parent->yend = zone_child->yend;
      zone_parent->total_x += zone_child->total_x;
      zone_parent->total_y += zone_child->total_y;
      zone_parent->surface_size += zone_child->surface_size;
    }
  }
}

static void
generate_final_zone(GArray *zoneid, GArray *zonearray, GArray **ret_zonearray,
    gint surface_min, gint surface_max)
{
  /* Ignore all child, we just care about root nodes */
  gint i;
  GArray *root_zonearray;
  Zone *zone;

  root_zonearray = g_array_sized_new(FALSE, FALSE, sizeof(Zone),
      zonearray->len);

  for(i=0; i<zonearray->len; i++) {
    if(i == g_array_index(zoneid, gint, i)) {
      /* this is root node */
      zone = &g_array_index(zonearray, Zone, i);

      /* filter root node if total surface area is out of limited */
      if((zone->surface_size > surface_max) ||
         (zone->surface_size < surface_min))
        continue;

      g_array_append_val(root_zonearray, *zone);
    }
  }
  *ret_zonearray = root_zonearray;
}

void
find_zones(guint8* graybuf, gint width, gint height, guint threshold,
    gint surface_min, gint surface_max, gint *markbuf, GArray **ret_zonearray)
{
  GArray *zoneid, *zonearray;
  gint *prevline_buf, *curline_buf;
  gint x, y;
  gint zone_mark = 1;
  gint index = 0;

  create_zones(&zoneid, &zonearray, DEFAULT_ZONE_ARRAY_SIZE);

  prevline_buf = markbuf;
  curline_buf = markbuf;
  if(graybuf[index++] > threshold) {
    *curline_buf++ = zone_mark;
    update_zone(zoneid, zonearray, 0, 0, zone_mark);
  } else {
    *curline_buf++ = 0;
  }
  for(x=1; x<width; x++) {
    if(graybuf[index++] > threshold) {
      gint prev_id;
      prev_id = *(curline_buf-1);
      if(prev_id == 0)
        prev_id = ++zone_mark;
      update_zone(zoneid, zonearray, x, 0, prev_id);
      *curline_buf++ = prev_id;
    } else {
      *curline_buf++ = 0;
    } 
  }

  for(y=1; y<height; y++) {
    /* 1st column */
    if(graybuf[index++] > threshold) {
      gint prev_id;
      prev_id = *prevline_buf++;
      if(!prev_id) {
        prev_id = *prevline_buf;
        if(!prev_id)
          prev_id = ++zone_mark;
      }
      update_zone(zoneid, zonearray, 0, y, prev_id);
      *curline_buf++ = prev_id;
    } else {
      prevline_buf++;
      *curline_buf++ = 0;
    }
    /* middle column */
    for(x=1; x<width-1; x++) {
      if(graybuf[index++] > threshold) {
        gint prev_id;
        prev_id = *(curline_buf-1);
        if(!prev_id) {
          prev_id = *(prevline_buf-1);
          if(!prev_id) {
            prev_id = *prevline_buf;
              if(!prev_id) {
                prev_id = *(prevline_buf+1);
                if(!prev_id)
                   prev_id = ++zone_mark;
            }
          }
        }
        prevline_buf++;
        update_zone(zoneid, zonearray, x, y, prev_id);
        *curline_buf++ = prev_id;
        /* join marker if needed */
        if (*prevline_buf && (prev_id != *prevline_buf)) {
          join_zones(zoneid, *prevline_buf, prev_id);
        }
      } else {
        prevline_buf++;
        *curline_buf++ = 0;
      }
    }
    /* last column */
    if(graybuf[index++] > threshold) {
      gint prev_id;
      prev_id = *(curline_buf-1);
      if(!prev_id) {
        prev_id = *(prevline_buf-1);
        if(!prev_id) {
          prev_id = *prevline_buf;
          if(!prev_id)
             prev_id = ++zone_mark;
        }
      }
      prevline_buf++;
      *curline_buf++ = prev_id;
      update_zone(zoneid, zonearray, x, y, prev_id);
    } else {
      prevline_buf++;
      *curline_buf++ = 0;
    }
  }

  /* finally count all root node and get the result */
  zone_root_count(zoneid, zonearray);
  generate_final_zone(zoneid, zonearray, ret_zonearray, surface_min,
    surface_max);
  g_array_free(zoneid, TRUE);
  g_array_free(zonearray, TRUE);
}

