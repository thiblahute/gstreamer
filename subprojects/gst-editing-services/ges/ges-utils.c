/* GStreamer Editing Services
 * Copyright (C) 2010 Edward Hervey <edward.hervey@collabora.co.uk>
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
 * SECTION:ges-utils
 * @title: GES utilities
 * @short_description: Convenience methods
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "ges-internal.h"
#include "ges-timeline.h"
#include "ges-track.h"
#include "ges-layer.h"
#include "ges.h"
#include <gst/base/base.h>

static GESConverterType __converter_type = GES_CONVERTER_SOFTWARE;
static GstElement *compositor_pad_creator = NULL;
static GstElementFactory *compositor_factory = NULL;

/**
 * ges_timeline_new_audio_video:
 *
 * Creates a new timeline containing a single #GESAudioTrack and a
 * single #GESVideoTrack.
 *
 * Returns: (transfer floating): The new timeline.
 */

GESTimeline *
ges_timeline_new_audio_video (void)
{
  GESTrack *tracka, *trackv;
  GESTimeline *timeline;

  /* This is our main GESTimeline */
  timeline = ges_timeline_new ();

  tracka = GES_TRACK (ges_audio_track_new ());
  trackv = GES_TRACK (ges_video_track_new ());

  if (!ges_timeline_add_track (timeline, trackv) ||
      !ges_timeline_add_track (timeline, tracka)) {
    gst_object_unref (timeline);
    timeline = NULL;
  }

  return timeline;
}

/* Internal utilities */
gint
element_start_compare (GESTimelineElement * a, GESTimelineElement * b)
{
  if (a->start == b->start) {
    if (a->priority < b->priority)
      return -1;
    if (a->priority > b->priority)
      return 1;
    if (a->duration < b->duration)
      return -1;
    if (a->duration > b->duration)
      return 1;
    return 0;
  } else if (a->start < b->start)
    return -1;

  return 1;
}

gint
element_end_compare (GESTimelineElement * a, GESTimelineElement * b)
{
  if (GES_TIMELINE_ELEMENT_END (a) == GES_TIMELINE_ELEMENT_END (b)) {
    if (a->priority < b->priority)
      return -1;
    if (a->priority > b->priority)
      return 1;
    if (a->duration < b->duration)
      return -1;
    if (a->duration > b->duration)
      return 1;
    return 0;
  } else if (GES_TIMELINE_ELEMENT_END (a) < (GES_TIMELINE_ELEMENT_END (b)))
    return -1;

  return 1;
}

gboolean
ges_pspec_equal (gconstpointer key_spec_1, gconstpointer key_spec_2)
{
  const GParamSpec *key1 = key_spec_1;
  const GParamSpec *key2 = key_spec_2;

  return (key1->owner_type == key2->owner_type &&
      strcmp (key1->name, key2->name) == 0);
}

guint
ges_pspec_hash (gconstpointer key_spec)
{
  const GParamSpec *key = key_spec;
  const gchar *p;
  guint h = key->owner_type;

  for (p = key->name; *p; p++)
    h = (h << 5) - h + *p;

  return h;
}

static GstElement *
get_internal_mixer (GstElementFactory * factory)
{
  GstElement *mixer = NULL, *elem = NULL;

  if (g_type_is_a (gst_element_factory_get_element_type (factory),
          GST_TYPE_AGGREGATOR)) {
    return gst_element_factory_create (factory, NULL);
  }

  if (!g_type_is_a (gst_element_factory_get_element_type (factory),
          GST_TYPE_BIN)) {
    goto done;
  }

  GParamSpec *pspec;

  elem = gst_element_factory_create (factory, NULL);

  /* Checks whether this element has mixer property and the internal element
   * is aggregator subclass */
  if (!elem) {
    goto done;
  }

  pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (elem), "mixer");
  if (!pspec) {
    goto done;
  }

  if (!g_type_is_a (pspec->value_type, GST_TYPE_ELEMENT)) {
    goto done;
  }

  g_object_get (elem, "mixer", &mixer, NULL);
  if (!mixer) {
    goto done;
  }

  if (!GST_IS_AGGREGATOR (mixer)) {
    gst_clear_object (&mixer);
  }

done:
  gst_clear_object (&elem);

  return mixer;
}

static gboolean
find_compositor (GstPluginFeature * feature, gpointer udata)
{
  gboolean res = FALSE;
  const gchar *klass;
  GstPluginFeature *loaded_feature = NULL;

  if (G_UNLIKELY (!GST_IS_ELEMENT_FACTORY (feature)))
    return FALSE;

  klass = gst_element_factory_get_metadata (GST_ELEMENT_FACTORY_CAST (feature),
      GST_ELEMENT_METADATA_KLASS);

  if (strstr (klass, "Compositor") == NULL)
    return FALSE;

  loaded_feature = gst_plugin_feature_load (feature);
  if (!loaded_feature) {
    return FALSE;
  }

  /* glvideomixer consists of bin with internal mixer element */
  GstElement *mixer = get_internal_mixer (GST_ELEMENT_FACTORY_CAST (feature));

  if (!mixer) {
    goto done;
  }
  res = TRUE;

  gst_object_unref (mixer);
  const gchar *needed_props[] = { "width", "height", "xpos", "ypos" };
  GObjectClass *objklass =
      g_type_class_ref (gst_element_factory_get_element_type
      (GST_ELEMENT_FACTORY (loaded_feature)));
  GstPadTemplate *templ =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (objklass),
      "sink_%u");

  g_type_class_unref (objklass);
  if (!templ) {
    GST_INFO_OBJECT (loaded_feature, "No sink template found, ignoring");
    res = FALSE;
    goto done;
  }

  GType pad_type;
  g_object_get (templ, "gtype", &pad_type, NULL);
  objklass = g_type_class_ref (pad_type);
  for (gint i = 0; i < G_N_ELEMENTS (needed_props); i++) {
    GParamSpec *pspec;

    if (!(pspec = g_object_class_find_property (objklass, needed_props[i]))) {
      GST_INFO_OBJECT (loaded_feature, "No property %s found, ignoring",
          needed_props[i]);
      res = FALSE;
      break;
    }

    if (pspec->value_type != G_TYPE_INT && pspec->value_type != G_TYPE_FLOAT
        && pspec->value_type != G_TYPE_DOUBLE) {
      GST_INFO_OBJECT (loaded_feature,
          "Property %s is not of type int or float, or double, ignoring",
          needed_props[i]);
      res = FALSE;
      break;
    }
  }
  g_type_class_unref (objklass);

done:
  gst_object_unref (loaded_feature);
  return res;
}

gboolean
ges_util_structure_get_clocktime (GstStructure * structure, const gchar * name,
    GstClockTime * val, GESFrameNumber * frames)
{
  gboolean found = FALSE;

  const GValue *gvalue;

  if (!val && !frames)
    return FALSE;

  gvalue = gst_structure_get_value (structure, name);
  if (!gvalue)
    return FALSE;

  if (frames)
    *frames = GES_FRAME_NUMBER_NONE;

  found = TRUE;
  if (val && G_VALUE_TYPE (gvalue) == GST_TYPE_CLOCK_TIME) {
    *val = (GstClockTime) g_value_get_uint64 (gvalue);
  } else if (val && G_VALUE_TYPE (gvalue) == G_TYPE_UINT64) {
    *val = (GstClockTime) g_value_get_uint64 (gvalue);
  } else if (val && G_VALUE_TYPE (gvalue) == G_TYPE_UINT) {
    *val = (GstClockTime) g_value_get_uint (gvalue);
  } else if (val && G_VALUE_TYPE (gvalue) == G_TYPE_INT) {
    *val = (GstClockTime) g_value_get_int (gvalue);
  } else if (val && G_VALUE_TYPE (gvalue) == G_TYPE_INT64) {
    *val = (GstClockTime) g_value_get_int64 (gvalue);
  } else if (val && G_VALUE_TYPE (gvalue) == G_TYPE_DOUBLE) {
    gdouble d = g_value_get_double (gvalue);

    if (d == -1.0)
      *val = GST_CLOCK_TIME_NONE;
    else
      *val = d * GST_SECOND;
  } else if (frames && G_VALUE_TYPE (gvalue) == G_TYPE_STRING) {
    const gchar *str = g_value_get_string (gvalue);

    found = FALSE;
    if (str && str[0] == 'f') {
      GValue v = G_VALUE_INIT;

      g_value_init (&v, G_TYPE_UINT64);
      if (gst_value_deserialize (&v, &str[1])) {
        *frames = g_value_get_uint64 (&v);
        if (val)
          *val = GST_CLOCK_TIME_NONE;
        found = TRUE;
      }
      g_value_reset (&v);
    }
  } else {
    found = FALSE;

  }

  return found;
}

static inline void
_init_value_from_spec_for_serialization (GValue * value, GParamSpec * spec)
{

  if (g_type_is_a (spec->value_type, G_TYPE_ENUM) ||
      g_type_is_a (spec->value_type, G_TYPE_FLAGS))
    g_value_init (value, G_TYPE_INT);
  else
    g_value_init (value, spec->value_type);
}

GstStructure *
ges_util_object_properties_to_structure (GObject * object,
    const gchar ** ignored_fields)
{
  guint n_props, j;
  GParamSpec *spec, **pspecs;
  GObjectClass *class = G_OBJECT_GET_CLASS (object);
  GstStructure *structure = gst_structure_new_empty ("properties");

  pspecs = g_object_class_list_properties (class, &n_props);
  for (j = 0; j < n_props; j++) {
    GValue val = { 0 };


    spec = pspecs[j];
    if (!ges_util_can_serialize_spec (spec))
      continue;

    if (ignored_fields) {
      if (g_strv_contains ((const gchar * const *) ignored_fields, spec->name))
        continue;
    }

    _init_value_from_spec_for_serialization (&val, spec);
    g_object_get_property (object, spec->name, &val);

    const GValue *default_val = g_param_spec_get_default_value (spec);
    if (g_type_is_a (spec->value_type, G_TYPE_ENUM) ||
        g_type_is_a (spec->value_type, G_TYPE_FLAGS)) {
      gint default_int = g_type_is_a (spec->value_type,
          G_TYPE_ENUM) ? g_value_get_enum (default_val) :
          g_value_get_flags (default_val);
      if (g_value_get_int (&val) == default_int) {
        GST_INFO ("Ignoring %s as it is using the default value", spec->name);
        goto next;
      }

    } else {
      if (gst_value_compare (default_val, &val) == GST_VALUE_EQUAL) {
        GST_INFO ("Ignoring %s as it is using the default value", spec->name);
        goto next;
      }
    }

    if (spec->value_type == GST_TYPE_CAPS) {
      gchar *caps_str;
      const GstCaps *caps = gst_value_get_caps (&val);

      caps_str = gst_caps_to_string (caps);
      gst_structure_set (structure, spec->name, G_TYPE_STRING, caps_str, NULL);
      g_free (caps_str);
      goto next;
    }

    gst_structure_set_value (structure, spec->name, &val);

  next:
    g_value_unset (&val);
  }
  g_free (pspecs);


  return structure;
}

GstPad *
ges_compositor_pad_new (void)
{
  ges_get_compositor_factory ();

  if (!compositor_pad_creator)
    return NULL;

  GstPad *res =
      gst_element_request_pad_simple (compositor_pad_creator, "sink_%u");

  return res;
}

GstElementFactory *
ges_get_compositor_factory (void)
{
  GList *result;

  if (compositor_factory)
    return compositor_factory;

  result = gst_registry_feature_filter (gst_registry_get (),
      (GstPluginFeatureFilter) find_compositor, FALSE, NULL);

  /* sort on rank and name */
  result = g_list_sort (result, gst_plugin_feature_rank_compare_func);
  g_assert (result);

  compositor_factory = result->data;

  GObjectClass *klass =
      g_type_class_ref (gst_element_factory_get_element_type
      (GST_ELEMENT_FACTORY (compositor_factory)));
  compositor_pad_creator = get_internal_mixer (compositor_factory);

  g_type_class_unref (klass);
  gst_plugin_feature_list_free (result);

  return compositor_factory;
}

void
ges_idle_add (GSourceFunc func, gpointer udata, GDestroyNotify notify)
{
  GMainContext *context = g_main_context_get_thread_default ();
  GSource *source = g_idle_source_new ();
  if (!context)
    context = g_main_context_default ();

  g_source_set_callback (source, func, udata, notify);
  g_source_attach (source, context);

}

gboolean
ges_nle_composition_add_object (GstElement * comp, GstElement * object)
{
  return gst_bin_add (GST_BIN (comp), object);
}

gboolean
ges_nle_composition_remove_object (GstElement * comp, GstElement * object)
{
  return gst_bin_remove (GST_BIN (comp), object);
}

gboolean
ges_nle_object_commit (GstElement * nlesource, gboolean recurse)
{
  gboolean ret;

  g_signal_emit_by_name (nlesource, "commit", recurse, &ret);

  return ret;
}

GESConverterType
ges_converter_type (void)
{
  static gboolean checked = FALSE;

  if (checked)
    return __converter_type;

  checked = TRUE;

  const gchar *envvar = g_getenv ("GES_CONVERTER_TYPE");


  if (!envvar) {
    const gchar *autoconvert = g_getenv ("GST_USE_AUTOCONVERT");

    envvar = "software";
    if (!g_strcmp0 (autoconvert, "1") ||
        !g_strcmp0 (autoconvert, "yes") ||
        !g_strcmp0 (autoconvert, "true") ||
        !g_strcmp0 (autoconvert, "on") || !g_strcmp0 (autoconvert, "y")) {
      envvar = "auto";
    }
  }

  GST_DEBUG ("---> CONVERTER TYPE: %s", envvar);
  envvar = g_ascii_strdown (envvar, -1);
  if (!g_strcmp0 (envvar, "auto"))
    __converter_type = GES_CONVERTER_AUTO;
  else if (!g_strcmp0 (envvar, "software"))
    __converter_type = GES_CONVERTER_SOFTWARE;
  else if (!g_strcmp0 (envvar, "gl"))
    __converter_type = GES_CONVERTER_GL;
  else
    g_warning ("Unknown value for GST_USE_AUTOCONVERT: %s", envvar);

  return __converter_type;
}

G_GNUC_INTERNAL const gchar *
ges_videoconvert_bin_desc (void)
{
  switch (ges_converter_type ()) {
    case GES_CONVERTER_SOFTWARE:
      return "videoconvert";
    case GES_CONVERTER_AUTO:
      return "autovideoconvert";
    case GES_CONVERTER_GL:
      return "glfilterbin filter=identity";
    default:
      g_assert_not_reached ();
  }
}

G_GNUC_INTERNAL GstElement *
ges_videoconvert_make (void)
{
  GstElement *element = NULL;
  GError *error = NULL;
  const gchar *bin_desc = ges_videoconvert_bin_desc ();

  element = gst_parse_bin_from_description (bin_desc, TRUE, &error);
  if (error) {
    GST_ERROR ("Failed to create %s: %s", bin_desc, error->message);
    g_clear_error (&error);
    return NULL;
  }

  return element;
}

G_GNUC_INTERNAL GstElement *
ges_videoconvert_scale_make (void)
{
  GstElement *element = NULL;
  GError *error = NULL;
  const gchar *bin_desc = NULL;

  switch (ges_converter_type ()) {
    case GES_CONVERTER_SOFTWARE:
      bin_desc = "videoconvertscale";
      break;
    case GES_CONVERTER_AUTO:
      bin_desc = "autovideoconvertscale";
      break;
    case GES_CONVERTER_GL:
      bin_desc = "glfilterbin filter=glcolorscale";
      break;
  }

  element = gst_parse_bin_from_description (bin_desc, TRUE, &error);
  if (error) {
    GST_ERROR ("Failed to create %s: %s", bin_desc, error->message);
    g_clear_error (&error);
    return NULL;
  }

  return element;
}

G_GNUC_INTERNAL GstElement *
ges_deinterlace_make (void)
{
  GstElement *element = NULL;
  GError *error = NULL;
  const gchar *bin_desc = NULL;

  switch (ges_converter_type ()) {
    case GES_CONVERTER_SOFTWARE:
      bin_desc = "deinterlace";
      break;
    case GES_CONVERTER_AUTO:
      bin_desc = "autodeinterlace";
      break;
    case GES_CONVERTER_GL:
      bin_desc = "glfilterbin filter=gldeinterlace";
      break;
  }

  element = gst_parse_bin_from_description (bin_desc, TRUE, &error);
  if (error) {
    GST_ERROR ("Failed to create %s: %s", bin_desc, error->message);
    g_clear_error (&error);
    return NULL;
  }

  return element;
}

G_GNUC_INTERNAL GstElement *
ges_video_flip_make (void)
{
  GstElement *element = NULL;
  GError *error = NULL;
  const gchar *bin_desc = NULL;

  switch (ges_converter_type ()) {
    case GES_CONVERTER_SOFTWARE:
      bin_desc = "videoflip video-direction=auto";
      break;
    case GES_CONVERTER_AUTO:
      bin_desc = "autovideoflip video-direction=auto";
      break;
    case GES_CONVERTER_GL:
      bin_desc = "glfilterbin filter=\"glvideoflip video-direction=auto\"";
      break;
  }

  element = gst_parse_bin_from_description (bin_desc, TRUE, &error);
  if (error) {
    GST_ERROR ("Failed to create %s: %s", bin_desc, error->message);
    g_clear_error (&error);
    return NULL;
  }

  return element;
}
