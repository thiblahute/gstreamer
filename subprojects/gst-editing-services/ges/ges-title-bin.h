/* GStreamer Editing Services
 * Copyright (C) 2025 Thibault Saunier <tsaunier@igalia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#pragma once

#include <gst/gst.h>
#include <ges/ges.h>

G_BEGIN_DECLS

typedef struct _GESTitleBin GESTitleBin;
typedef struct _GESTitleBinClass GESTitleBinClass;

/**
 * GESTitleBin:
 *
 * Bin for rendering text titles with background
 */
struct _GESTitleBin
{
  GstBin parent;

  GESTitleSource *source;

  /* Elements */
  GstElement *background_el;
  GstElement *text_el;
  GstElement *glupload_el;
  GstElement *overlaycomposition_el;
  GstElement *freeze_el;

  GList *control_bindings;
};

/**
 * GESTitleBinClass:
 *
 * Class for #GESTitleBin
 */
struct _GESTitleBinClass
{
  GstBinClass parent_class;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};


GESTitleBin *
ges_title_bin_new (GESTitleSource * source, const gchar * name);

GstElement * ges_title_bin_get_text_element (GESTitleBin * self);
GstElement * ges_title_bin_get_background_element (GESTitleBin * self);

void ges_title_bin_configure_text (GESTitleBin * self,
                                   const gchar * text,
                                   const gchar * font_desc,
                                   gint valign,
                                   gint halign,
                                   guint color,
                                   gdouble xpos,
                                   gdouble ypos);

void ges_title_bin_configure_background (GESTitleBin * self,
                                         gint pattern,
                                         guint color);

G_END_DECLS
