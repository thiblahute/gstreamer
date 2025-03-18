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

/**
 * SECTION:gestitlebin
 * @title: GESTitleBin
 * @short_description: a bin for rendering text titles with background
 *
 * #GESTitleBin is a GstBin subclass that implements the rendering
 * pipeline for title elements in GES.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ges-internal.h"
#include "ges-title-bin.h"

#define GES_TYPE_TITLE_BIN            (ges_title_bin_get_type ())
#define GES_TITLE_BIN(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_TITLE_BIN, GESTitleBin))
#define GES_TITLE_BIN_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_TITLE_BIN, GESTitleBinClass))
#define GES_IS_TITLE_BIN(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_TITLE_BIN))
#define GES_IS_TITLE_BIN_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_TITLE_BIN))
#define GES_TITLE_BIN_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_TITLE_BIN, GESTitleBinClass))

GType ges_title_bin_get_type (void);

GST_DEBUG_CATEGORY_STATIC (gestitlebin_debug);
#undef GST_CAT_DEFAULT
#define GST_CAT_DEFAULT gestitlebin_debug

#define ges_title_bin_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GESTitleBin, ges_title_bin, GST_TYPE_BIN,
    GST_DEBUG_CATEGORY_INIT (gestitlebin_debug, "gestitlebin",
        GST_DEBUG_FG_YELLOW, "gestitlebin");
    );

static GstStateChangeReturn
ges_title_bin_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GESTitleBin *self = GES_TITLE_BIN (element);

  /* Handle state changes */
  if (transition == GST_STATE_CHANGE_READY_TO_PAUSED) {
    gboolean has_relevant_bindings = FALSE;
    const gchar *frame_positioner_props[] = {
      "posx", "posy", "width", "height", "alpha",
      NULL
    };

    GST_OBJECT_LOCK (self);
    for (GList * props_it = self->control_bindings; props_it;
        props_it = props_it->next) {
      const gchar *prop_name = (const gchar *) props_it->data;
      gboolean is_frame_prop = FALSE;

      for (gint i = 0; i < G_N_ELEMENTS (frame_positioner_props); i++) {
        if (!g_strcmp0 (prop_name, frame_positioner_props[i])) {
          is_frame_prop = TRUE;
          break;
        }
      }

      if (!is_frame_prop) {
        has_relevant_bindings = TRUE;
        break;
      }
    }
    GST_OBJECT_UNLOCK (self);

    GST_DEBUG_OBJECT (self,
        "READY to PAUSED transition, has_relevant_bindings: %d",
        has_relevant_bindings);

    g_object_set (self->freeze_el, "allow-replace", has_relevant_bindings,
        NULL);
    g_object_set (self->background_el, "num-buffers",
        has_relevant_bindings ? 1 : -1, NULL);
  }

  /* Chain up */
  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return ret;
}

static void
_control_binding_added (GESTrackElement * _element, GstControlBinding * binding,
    GESTitleBin * self)
{
  GST_OBJECT_LOCK (self);
  self->control_bindings =
      g_list_prepend (self->control_bindings, g_strdup (binding->name));
  GST_OBJECT_UNLOCK (self);
}

static void
_control_binding_removed (GESTrackElement * _element,
    GstControlBinding * binding, GESTitleBin * self)
{
  GST_OBJECT_LOCK (self);
  GList *tmp = g_list_find_custom (self->control_bindings, binding->name,
      (GCompareFunc) g_strcmp0);
  gchar *binding_name = tmp->data;
  g_assert (tmp);
  self->control_bindings = g_list_remove (self->control_bindings, tmp);
  g_free (binding_name);
  GST_OBJECT_UNLOCK (self);
}

static void
ges_title_bin_dispose (GObject * object)
{
  GESTitleBin *self = GES_TITLE_BIN (object);

  g_signal_handlers_disconnect_by_func (self->source,
      G_CALLBACK (_control_binding_added), self);
  g_signal_handlers_disconnect_by_func (self->source,
      G_CALLBACK (_control_binding_removed), self);
}

static void
ges_title_bin_class_init (GESTitleBinClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  object_class->dispose = ges_title_bin_dispose;
  element_class->change_state = ges_title_bin_change_state;
}

static void
ges_title_bin_init (GESTitleBin * self)
{
  self->background_el = NULL;
  self->text_el = NULL;
  self->glupload_el = NULL;
  self->overlaycomposition_el = NULL;
  self->freeze_el = NULL;
}

#define MAKE_ELEMENT_AND_CHECK(el,factoryname, name)\
  el = gst_element_factory_make (factoryname, name); \
  if (!el) { \
    GST_ERROR_OBJECT (self, "Failed to create %s element", factoryname); \
    goto err; \
  } \
  g_object_ref_sink (el);


GESTitleBin *
ges_title_bin_new (GESTitleSource * source, const gchar * name)
{
  GESTitleBin *self;
  GstElement *background = NULL, *text = NULL, *freeze = NULL;
  GstElement *glupload = NULL, *overlaycomposition = NULL;
  GstPad *pad = NULL, *ghostpad = NULL;
  const gchar *bin_name;
  gboolean is_gl = ges_converter_type () == GES_CONVERTER_GL;

  bin_name = name ? name : "title-bin";
  self = g_object_new (GES_TYPE_TITLE_BIN, "name", bin_name, NULL);

  MAKE_ELEMENT_AND_CHECK (background, "videotestsrc",
      is_gl ? "gltitlesrc-bg" : "titlesrc-bg");
  MAKE_ELEMENT_AND_CHECK (text, "textoverlay", "titlesrc-text");
  MAKE_ELEMENT_AND_CHECK (freeze, "imagefreeze", "titlesrc-freeze");

  gst_bin_add_many (GST_BIN (self), background, text, freeze, NULL);
  if (!gst_element_link_pads_full (background, "src", text, "video_sink",
          GST_PAD_LINK_CHECK_NOTHING)) {
    GST_ERROR_OBJECT (self, "Failed to link videotestsrc to textoverlay");
    goto err;
  }

  self->background_el = background;
  self->text_el = text;
  self->freeze_el = freeze;

  /* If using GL, add GL elements */
  if (is_gl) {
    MAKE_ELEMENT_AND_CHECK (glupload, "glupload", "title-glupload");
    MAKE_ELEMENT_AND_CHECK (overlaycomposition, "gloverlaycompositor",
        "title-gloverlaycompositor");

    /* Add GL elements to bin */
    gst_bin_add_many (GST_BIN (self), glupload, overlaycomposition, NULL);

    /* Link text -> glupload -> overlaycomposition -> freeze */
    if (!gst_element_link_many (text, glupload, overlaycomposition, freeze,
            NULL)) {
      GST_ERROR_OBJECT (self,
          "Failed to link textoverlay -> GL elements -> imagefreeze");
      goto err;
    }

    /* Store references to GL elements */
    self->glupload_el = glupload;
    self->overlaycomposition_el = overlaycomposition;
    self->source = source;

    /* Create ghost pad from freeze src */
    pad = gst_element_get_static_pad (freeze, "src");
  } else {
    /* Link text -> freeze */
    if (!gst_element_link (text, freeze)) {
      GST_ERROR_OBJECT (self, "Failed to link textoverlay to imagefreeze");
      goto err;
    }

    /* Create ghost pad from freeze src */
    pad = gst_element_get_static_pad (freeze, "src");
  }

  /* Create ghost pad */
  ghostpad = gst_ghost_pad_new ("src", pad);
  gst_clear_object (&pad);

  /* Add ghost pad to bin */
  if (!gst_element_add_pad (GST_ELEMENT (self), ghostpad)) {
    GST_ERROR_OBJECT (self, "Failed to add ghost pad to bin");
    goto err;
  }

  g_signal_connect (source, "control-binding-added",
      G_CALLBACK (_control_binding_added), self);
  g_signal_connect (source, "control-binding-removed",
      G_CALLBACK (_control_binding_removed), self);

  return self;

err:
  gst_clear_object (&pad);
  gst_clear_object (&ghostpad);
  gst_clear_object (&background);
  gst_clear_object (&text);
  gst_clear_object (&freeze);
  gst_clear_object (&self);
  gst_clear_object (&glupload);
  gst_clear_object (&overlaycomposition);

  return NULL;
}

/**
 * ges_title_bin_get_text_element:
 * @self: A #GESTitleBin
 *
 * Gets the textoverlay element used in this bin.
 *
 * Returns: (transfer none): The textoverlay element
 */
GstElement *
ges_title_bin_get_text_element (GESTitleBin * self)
{
  return self->text_el;
}

/**
 * ges_title_bin_get_background_element:
 * @self: A #GESTitleBin
 *
 * Gets the videotestsrc element used for background in this bin.
 *
 * Returns: (transfer none): The videotestsrc element
 */
GstElement *
ges_title_bin_get_background_element (GESTitleBin * self)
{
  return self->background_el;
}

/**
 * ges_title_bin_configure_text:
 * @self: A #GESTitleBin
 * @text: (nullable): The text to display
 * @font_desc: (nullable): The font description
 * @valign: Vertical alignment
 * @halign: Horizontal alignment
 * @color: Text color
 * @xpos: X position
 * @ypos: Y position
 *
 * Configure the text properties of this title bin.
 */
void
ges_title_bin_configure_text (GESTitleBin * self,
    const gchar * text,
    const gchar * font_desc,
    gint valign, gint halign, guint color, gdouble xpos, gdouble ypos)
{
  if (self->text_el == NULL)
    return;

  if (text)
    g_object_set (self->text_el, "text", text, NULL);

  if (font_desc)
    g_object_set (self->text_el, "font-desc", font_desc, NULL);

  g_object_set (self->text_el,
      "valignment", valign,
      "halignment", halign, "color", color, "xpos", xpos, "ypos", ypos, NULL);
}

/**
 * ges_title_bin_configure_background:
 * @self: A #GESTitleBin
 * @pattern: The pattern to use
 * @color: Background color
 *
 * Configure the background properties of this title bin.
 */
void
ges_title_bin_configure_background (GESTitleBin * self,
    gint pattern, guint color)
{
  if (self->background_el == NULL)
    return;

  g_object_set (self->background_el,
      "pattern", pattern, "foreground-color", color, NULL);
}
