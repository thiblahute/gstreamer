/* GStreamer Editing Services
 * Copyright (C) 2020 Ubicast S.A
 *     Author: Thibault Saunier <tsaunier@igalia.com>
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

#include "gst/gstutils.h"
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ges-internal.h"
#include "ges-uri-source.h"

GST_DEBUG_CATEGORY_STATIC (uri_source_debug);
#undef GST_CAT_DEFAULT
#define GST_CAT_DEFAULT uri_source_debug

static void
ges_uri_source_init_debug (void)
{
  static gsize once = 0;

  if (g_once_init_enter (&once)) {
    GST_DEBUG_CATEGORY_INIT (uri_source_debug, "gesurisource", 0,
        "GES uri source");
    g_once_init_leave (&once, 1);
  }
}

#define DEFAULT_RAW_CAPS			\
  "video/x-raw; "				\
  "audio/x-raw; "				\
  "text/x-raw; "				\
  "subpicture/x-dvd; "			\
  "subpicture/x-pgs"

static GstStaticCaps default_raw_caps = GST_STATIC_CAPS (DEFAULT_RAW_CAPS);

static gboolean _ges_enable_uridecodepoolsrc = FALSE;

static inline gboolean
are_raw_caps (const GstCaps * caps)
{
  GstCaps *raw = gst_static_caps_get (&default_raw_caps);
  gboolean res = gst_caps_can_intersect (caps, raw);

  gst_caps_unref (raw);
  return res;
}

typedef enum
{
  GST_AUTOPLUG_SELECT_TRY,
  GST_AUTOPLUG_SELECT_EXPOSE,
  GST_AUTOPLUG_SELECT_SKIP,
} GstAutoplugSelectResult;

static gint
autoplug_select_cb (GstElement * bin, GstPad * pad, GstCaps * caps,
    GstElementFactory * factory, GESUriSource * self)
{
  GstElement *nlesrc;
  GstCaps *downstream_caps;
  GstQuery *segment_query = NULL;
  GstFormat segment_format;
  GstAutoplugSelectResult res = GST_AUTOPLUG_SELECT_TRY;
  gchar *stream_id = gst_pad_get_stream_id (pad);
  const gchar *wanted_id =
      gst_discoverer_stream_info_get_stream_id
      (ges_uri_source_asset_get_stream_info (GES_URI_SOURCE_ASSET
          (ges_extractable_get_asset (GES_EXTRACTABLE (self->element)))));
  gboolean wanted = !g_strcmp0 (stream_id, wanted_id);

  if (!ges_source_get_rendering_smartly (GES_SOURCE (self->element))) {
    if (!are_raw_caps (caps))
      goto done;

    if (!wanted) {
      GST_INFO_OBJECT (self->element, "Not matching stream id: %s -> SKIPPING",
          stream_id);
      res = GST_AUTOPLUG_SELECT_SKIP;
    } else {
      GST_INFO_OBJECT (self->element, "Using stream %s", stream_id);
    }
    goto done;
  }

  segment_query = gst_query_new_segment (GST_FORMAT_TIME);
  if (!gst_pad_query (pad, segment_query)) {
    GST_DEBUG_OBJECT (pad, "Could not query segment");

    goto done;
  }

  gst_query_parse_segment (segment_query, NULL, &segment_format, NULL, NULL);
  if (segment_format != GST_FORMAT_TIME) {
    GST_DEBUG_OBJECT (pad,
        "Segment not in %s != time for %" GST_PTR_FORMAT
        "... continue plugin elements", gst_format_get_name (segment_format),
        caps);

    goto done;
  }

  nlesrc = ges_track_element_get_nleobject (self->element);
  downstream_caps = gst_pad_peer_query_caps (nlesrc->srcpads->data, NULL);
  if (downstream_caps && gst_caps_can_intersect (downstream_caps, caps)) {
    if (wanted) {
      res = GST_AUTOPLUG_SELECT_EXPOSE;
      GST_INFO_OBJECT (self->element,
          "Exposing %" GST_PTR_FORMAT " with stream id: %s", caps, stream_id);
    } else {
      res = GST_AUTOPLUG_SELECT_SKIP;
      GST_DEBUG_OBJECT (self->element, "Totally skipping %s", stream_id);
    }
  }
  gst_clear_caps (&downstream_caps);

done:
  g_free (stream_id);
  gst_clear_query (&segment_query);

  return res;
}

static void
source_setup_cb (GstElement * decodebin, GstElement * source,
    GESUriSource * self)
{
  GstElementFactory *factory = gst_element_get_factory (source);

  if (!factory || g_strcmp0 (GST_OBJECT_NAME (factory), "gessrc")) {
    return;
  }

  GESTrack *track = ges_track_element_get_track (self->element);
  GESTimeline *subtimeline;

  g_object_get (source, "timeline", &subtimeline, NULL);
  GstStreamCollection *subtimeline_collection =
      ges_timeline_get_stream_collection (subtimeline);

  ges_track_select_subtimeline_streams (track, subtimeline_collection,
      GST_ELEMENT (subtimeline));
}

gboolean
ges_source_uses_uridecodepoolsrc (GESSource * self)
{
  static gsize once = 0;
  const gchar *envvar;

  ges_uri_source_init_debug ();
  if (g_once_init_enter (&once)) {
    envvar = g_getenv ("GES_ENABLE_URIDECODEPOOLSRC");

    gboolean uridecodepoolsrc_enabled = !g_strcmp0 (envvar, "1") ||
        !g_strcmp0 (envvar, "yes") ||
        !g_strcmp0 (envvar, "true") ||
        !g_strcmp0 (envvar, "on") || !g_strcmp0 (envvar, "y");

    if (uridecodepoolsrc_enabled) {
      GstPluginFeature *feature =
          gst_registry_find_feature (gst_registry_get (), "uridecodepoolsrc",
          GST_TYPE_ELEMENT_FACTORY);

      if (feature) {
        _ges_enable_uridecodepoolsrc = TRUE;
        gst_object_unref (feature);
      } else {
        g_error ("uridecodepoolsrc is not available while enabled.");
      }

    }

    GST_INFO ("uridecodepoolsrc %sabled",
        _ges_enable_uridecodepoolsrc ? "en" : "dis");

    g_once_init_leave (&once, 1);
  }

  return _ges_enable_uridecodepoolsrc;
}

static GstEvent *
ges_uri_source_query_seek (GESUriSource * self, GstEvent * seek)
{
  GstElement *nlesrc = ges_track_element_get_nleobject (self->element);

  g_assert (seek);

  GstStructure *structure =
      gst_structure_new ("translate-seek", "seek", GST_TYPE_EVENT, seek, NULL);
  GstQuery *query_translate_seek =
      gst_query_new_custom (GST_QUERY_CUSTOM, structure);

  if (!gst_element_query (nlesrc, query_translate_seek)) {
    GST_ERROR_OBJECT (nlesrc, "Failed to translate seek!!");
    return NULL;
  }

  GstEvent *translated_seek = NULL;
  gst_structure_get (structure, "translated-seek", GST_TYPE_EVENT,
      &translated_seek, NULL);
  g_assert (translated_seek);

  gst_query_unref (query_translate_seek);
  gst_event_unref (seek);

  return translated_seek;
}

static GstEvent *
uridecodepoolsrc_get_initial_seek_cb (GstElement * uridecodepoolsrc,
    GESUriSource * self)
{
  if (self->controls_nested_timeline) {
    GST_INFO_OBJECT (uridecodepoolsrc,
        "Controls a nested timeline not sending initial seek as the deepest timeline will do it itself");

    return NULL;
  }

  GList *toplevel_src_node = g_list_last (self->parent_ges_uri_sources);
  GESTimeline *toplevel_timeline = toplevel_src_node ?
      GES_TIMELINE_ELEMENT_TIMELINE (((GESUriSource *)
          toplevel_src_node->data)->element) : NULL;
  GstEvent *seek = gst_event_new_seek (1.0,
      GST_FORMAT_TIME,
      GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE,
      GST_SEEK_TYPE_SET,
      0,
      GST_SEEK_TYPE_SET,
      ges_timeline_get_duration (toplevel_timeline ? toplevel_timeline :
          GES_TIMELINE_ELEMENT_TIMELINE (self->element))
      );

  /* TODO time-effect: Also add time effect support in ges_pipeline_pool_manager_prepare_pipelines_around */
  GST_FIXME_OBJECT (self->element, "FIXME Add support for time effects");
  for (GList * tmp = toplevel_src_node; tmp; tmp = tmp->prev) {
    seek = ges_uri_source_query_seek (tmp->data, seek);
    GST_DEBUG_OBJECT (uridecodepoolsrc, "Parent: %s",
        GES_TIMELINE_ELEMENT_NAME (((GESUriSource *) tmp->data)->element));
  }

  seek = ges_uri_source_query_seek (self, seek);
  GST_DEBUG_OBJECT (self->element, "%s initlial seek: %" GST_PTR_FORMAT,
      GES_TIMELINE_ELEMENT_NAME (self->element), seek);

  return seek;
}

static void
uridecodepoolsrc_setup_parent_sources (GstElement * uridecodepoolsrc,
    GESUriSource * self)
{
  GESUriSource *child_ges_source =
      g_object_get_data (G_OBJECT (uridecodepoolsrc), "__gesurisource_data__");
  g_assert (child_ges_source);

  for (GList * tmp = child_ges_source->parent_ges_uri_sources; tmp;
      tmp = tmp->next) {
  }

  if (!g_list_find (child_ges_source->parent_ges_uri_sources, self)) {
    child_ges_source->parent_ges_uri_sources =
        g_list_prepend (child_ges_source->parent_ges_uri_sources, self);
  }

  for (GList * tmp = self->parent_ges_uri_sources; tmp; tmp = tmp->next) {
    if (!g_list_find (child_ges_source->parent_ges_uri_sources, tmp->data)) {
      child_ges_source->parent_ges_uri_sources =
          g_list_append (child_ges_source->parent_ges_uri_sources, tmp->data);
    }
  }

  for (GList * tmp = child_ges_source->parent_ges_uri_sources; tmp;
      tmp = tmp->next) {
  }
}

static gboolean
setup_uridecodepool_srcs (GNode * node, GESUriSource * self)
{
  GESUriSource *child_source = NULL;
  if (GES_IS_AUDIO_URI_SOURCE (node->data))
    child_source = GES_AUDIO_URI_SOURCE (node->data)->priv;
  else if (GES_IS_VIDEO_URI_SOURCE (node->data))
    child_source = GES_VIDEO_URI_SOURCE (node->data)->priv;

  if (child_source) {
    uridecodepoolsrc_setup_parent_sources (child_source->decodebin, self);
  }

  return FALSE;
}

static void
uridecodepoolsrc_deep_element_added_cb (GstPipeline * pipeline, GstBin * bin,
    GstElement * element, GESUriSource * self)
{
  if (GES_IS_TIMELINE (element)) {
    g_node_traverse (timeline_get_tree (GES_TIMELINE (element)), G_IN_ORDER,
        G_TRAVERSE_LEAVES, -1, (GNodeTraverseFunc) setup_uridecodepool_srcs,
        self);
    timeline_set_parent_uri_source (GES_TIMELINE (element),
        (GESSource *) self->element);
  }
}

static void
uridecodepoolsrc_pipeline_notify_cb (GstElement * decodebin,
    GParamSpec * arg G_GNUC_UNUSED, GESUriSource * self)
{
  GstPipeline *pipeline, *prev_pipeline;

  g_object_get (decodebin, "pipeline", &pipeline, NULL);

  prev_pipeline = g_weak_ref_get (&self->uridecodepool_pipeline);
  if (prev_pipeline) {
    g_signal_handlers_disconnect_by_func (prev_pipeline,
        uridecodepoolsrc_pipeline_notify_cb, self);
  }

  if (pipeline) {
    g_signal_connect_data (pipeline, "deep-element-added",
        G_CALLBACK (uridecodepoolsrc_deep_element_added_cb), self, NULL, 0);
  }
  g_weak_ref_set (&self->uridecodepool_pipeline, pipeline);

  GST_DEBUG_OBJECT (self->element, "Pipeline changed: %" GST_PTR_FORMAT,
      pipeline);
}

static GstElement *
uridecodepoolsrc_create_filter (GstElement * uridecodepoolsrc,
    GstElement * _underlying_pipeline, GstPad * srcpad,
    const gchar * filter_desc)
{
  GError *err = NULL;
  GstElement *filter = gst_parse_bin_from_description (filter_desc, TRUE, &err);

  if (!filter) {
    GST_INFO_OBJECT (uridecodepoolsrc, "Could not create filter: %s: %s",
        filter_desc, err->message);
    g_clear_error (&err);
    return NULL;
  }

  return g_object_ref_sink (filter);
}

static GstElement *
ges_uri_source_create_uridecodepoolsrc (GESUriSource * self)
{
  GESTrack *track;
  GstElement *decodebin;
  const gchar *filter = NULL;
  GESUriSourceAsset *asset = GES_URI_SOURCE_ASSET
      (ges_extractable_get_asset (GES_EXTRACTABLE (self->element)));
  const gchar *wanted_id = gst_discoverer_stream_info_get_stream_id
      (ges_uri_source_asset_get_stream_info (asset));
  const GESUriClipAsset *clip_asset =
      ges_uri_source_asset_get_filesource_asset (asset);

  track = ges_track_element_get_track (self->element);

  gchar *name = g_strdup_printf ("%s-uridecodepoolsrc",
      GES_TIMELINE_ELEMENT_NAME (self->element));
  self->decodebin = decodebin =
      gst_element_factory_make ("uridecodepoolsrc", name);
  g_object_set_data (G_OBJECT (decodebin), "__gesurisource_data__", self);
  g_free (name);
  GST_DEBUG_OBJECT (self->element,
      "%" GST_PTR_FORMAT " - Track! %" GST_PTR_FORMAT, self->decodebin, track);
  GstCaps *caps = NULL;
  if (GES_IS_VIDEO_SOURCE (self->element)) {

    if (ges_converter_type () == GES_CONVERTER_SOFTWARE)
      caps = gst_caps_from_string ("video/x-raw");
    else
      caps = gst_caps_from_string ("video/x-raw(ANY)");

    if (ges_uri_source_asset_is_image (asset)) {
      if (ges_converter_type () == GES_CONVERTER_GL) {
        filter =
            "glupload ! glcolorconvert ! capsfilter caps=\"video/x-raw(memory:GLMemory),format=RGBA\" ! imagefreeze";
      } else {
        filter = "imagefreeze";
      }
    } else {
      if (ges_converter_type () == GES_CONVERTER_GL) {
        filter =
            "glupload ! glcolorconvert ! capsfilter caps=\"video/x-raw(memory:GLMemory),format=RGBA\" ! segmentclipper";
      } else {
        filter = "segmentclipper";
      }
    }
    GST_DEBUG ("Using caps: %" GST_PTR_FORMAT " for uridecodepoolsrc", caps);
  } else if (GES_IS_AUDIO_SOURCE (self->element)) {
    caps = gst_caps_new_empty_simple ("audio/x-raw");
    filter = "audioconvert ! scaletempo ! audioconvert";
  } else {
    g_assert_not_reached ();
  }

  g_signal_connect_data (decodebin, "create-filter",
      G_CALLBACK (uridecodepoolsrc_create_filter), (gpointer) filter, NULL, 0);

  g_signal_connect (decodebin, "source-setup",
      G_CALLBACK (source_setup_cb), self);


  g_object_set (decodebin, "uri", self->uri, "stream-id", wanted_id, "caps",
      caps, NULL);

  g_signal_connect_data (decodebin, "get-initial-seek",
      G_CALLBACK (uridecodepoolsrc_get_initial_seek_cb), self, NULL, 0);
  if (clip_asset) {
    g_object_get (G_OBJECT (clip_asset), "is-nested-timeline",
        &self->controls_nested_timeline, NULL);
  }

  if (self->controls_nested_timeline) {
    g_signal_connect_data (decodebin, "notify::pipeline",
        G_CALLBACK (uridecodepoolsrc_pipeline_notify_cb), self, 0, 0);
    uridecodepoolsrc_pipeline_notify_cb (decodebin, NULL, self);
  }

  gst_caps_unref (caps);

  return decodebin;
}

GstElement *
ges_uri_source_create_source (GESUriSource * self)
{
  GESTrack *track;
  GstElement *decodebin;
  const GstCaps *caps = NULL;

  if (ges_source_uses_uridecodepoolsrc (GES_SOURCE (self->element)))
    return ges_uri_source_create_uridecodepoolsrc (self);

  track = ges_track_element_get_track (self->element);

  self->decodebin = decodebin = gst_element_factory_make ("uridecodebin", NULL);
  GST_DEBUG_OBJECT (self->element,
      "%" GST_PTR_FORMAT " - Track! %" GST_PTR_FORMAT, self->decodebin, track);

  if (track)
    caps = ges_track_get_caps (track);

  g_signal_connect (decodebin, "source-setup",
      G_CALLBACK (source_setup_cb), self);

  g_object_set (decodebin, "caps", caps,
      "expose-all-streams", FALSE, "uri", self->uri, NULL);
  g_signal_connect (decodebin, "autoplug-select",
      G_CALLBACK (autoplug_select_cb), self);

  return decodebin;
}

static void
ges_uri_source_track_set_cb (GESTrackElement * element,
    GParamSpec * arg G_GNUC_UNUSED, GESUriSource * self)
{
  GESTrack *track;
  const GstCaps *caps = NULL;

  if (!self->decodebin)
    return;

  track = ges_track_element_get_track (GES_TRACK_ELEMENT (element));
  if (!track)
    return;

  caps = ges_track_get_caps (track);

  GST_INFO_OBJECT (element,
      "Setting %" GST_PTR_FORMAT "caps to: %" GST_PTR_FORMAT, self->decodebin,
      caps);

  if (!ges_source_uses_uridecodepoolsrc (GES_SOURCE (self->element)))
    g_object_set (self->decodebin, "caps", caps, NULL);
}

void
ges_uri_source_init (GESTrackElement * element, GESUriSource * self)
{
  ges_uri_source_init_debug ();

  self->element = element;
  g_signal_connect (element, "notify::track",
      G_CALLBACK (ges_uri_source_track_set_cb), self);
}

gboolean
ges_uri_source_select_pad (GESSource * self, GstPad * pad)
{
  if (ges_source_uses_uridecodepoolsrc (self))
    return TRUE;

  gboolean res = TRUE;
  gboolean is_nested_timeline;
  GESUriSourceAsset *asset =
      GES_URI_SOURCE_ASSET (ges_extractable_get_asset (GES_EXTRACTABLE (self)));
  const GESUriClipAsset *clip_asset =
      ges_uri_source_asset_get_filesource_asset (asset);
  const gchar *wanted_stream_id = ges_asset_get_id (GES_ASSET (asset));
  gchar *stream_id;

  if (clip_asset) {
    g_object_get (G_OBJECT (clip_asset), "is-nested-timeline",
        &is_nested_timeline, NULL);

    if (is_nested_timeline) {
      GST_DEBUG_OBJECT (self, "Nested timeline track selection is handled"
          " by the timeline SELECT_STREAM events handling.");

      return TRUE;
    }
  }

  stream_id = gst_pad_get_stream_id (pad);
  res = !g_strcmp0 (stream_id, wanted_stream_id);

  GST_ERROR_OBJECT (self, "%s pad with stream id: %s as %s wanted",
      res ? "Using" : "Ignoring", stream_id, wanted_stream_id);
  g_free (stream_id);

  return res;
}


void
_deinit_playbin_pool_src (void)
{
  if (!_ges_enable_uridecodepoolsrc)
    return;

  GstElement *uridecodepoolsrc =
      gst_element_factory_make ("uridecodepoolsrc", NULL);
  if (!uridecodepoolsrc)
    return;

  GObject *pool =
      gst_child_proxy_get_child_by_name (GST_CHILD_PROXY (uridecodepoolsrc),
      "pool");
  g_signal_emit_by_name (pool, "deinit");
  gst_object_unref (pool);
  gst_object_unref (uridecodepoolsrc);
}
