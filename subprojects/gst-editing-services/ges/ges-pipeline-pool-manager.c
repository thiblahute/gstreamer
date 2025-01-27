#include "ges-pipeline-pool-manager.h"
#include "ges-internal.h"

#include <gst/video/video.h>


#undef GST_CAT_DEFAULT
#define GST_CAT_DEFAULT ges_pipeline_pool_manager_debug

/* FIXME Make that a public setting */
#define MAX_PRELOADED_SOURCES 10

GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

typedef struct
{
  GstElement *element;

  /* Not keeping ref as it is only used to do pointer comparison */
  GESTrack *track;

  GstClockTime start;
  GstClockTime end;

  GObject *decoderpipe;
} PooledSource;

static gint
compare_pooled_source (PooledSource * a, PooledSource * b)
{
  if (a->start == b->start) {
    if (a->end < b->end)
      return -1;
    if (a->end > b->end)
      return 1;
    return 0;
  } else if (a->start < b->start) {
    return -1;
  }

  return 1;
}


static void
pooled_source_clear (PooledSource * s)
{
  gst_object_unref (s->element);
}

void
ges_pipeline_pool_manager_prepare_pipelines_around (GESPipelinePoolManager *
    self, GESTrack * track, GstClockTime stack_start, GstClockTime stack_end)
{
  gboolean entered_window = FALSE;
  GstClockTime window_dur = 60 * GST_SECOND;
  GstClockTime window_start;
  GstClockTime window_stop = stack_end + window_dur;
  gint max_preloaded_sources = MAX_PRELOADED_SOURCES;

  if (!GES_IS_VIDEO_TRACK (track)) {
    GST_DEBUG_OBJECT (self->timeline,
        "Not preparing neighboors anything for %s track",
        G_OBJECT_TYPE_NAME (track));

    return;
  }

  if (!GST_CLOCK_TIME_IS_VALID (stack_start)
      || !GST_CLOCK_TIME_IS_VALID (stack_end)) {
    GST_INFO_OBJECT (track,
        "Got invalid stack start/end, not preparing anything.");
    return;
  }

  g_rec_mutex_lock (&self->lock);
  if (!self->pooled_sources) {
    g_rec_mutex_unlock (&self->lock);
    return;
  }


  GST_LOG_OBJECT (self->timeline, "Preparing pipelines around %" GST_TIME_FORMAT
      " - %" GST_TIME_FORMAT " window: [%" GST_TIMEP_FORMAT " - %"
      GST_TIMEP_FORMAT "]" " in %d soures", GST_TIME_ARGS (stack_start),
      GST_TIME_ARGS (stack_end), &window_start, &window_stop,
      self->pooled_sources->len);

  GstState state, pending;
  gboolean playing = (
      (gst_element_get_state (GST_ELEMENT (track), &state, &pending,
              0) == GST_STATE_CHANGE_SUCCESS) && state == GST_STATE_PLAYING
      && pending == GST_STATE_VOID_PENDING);
  if (self->rendering || playing) {
    GST_DEBUG_OBJECT (self->timeline, "We are %s not loading clips before",
        playing ? "playing" : "rendering");
    window_start = stack_start;
    max_preloaded_sources = max_preloaded_sources / 2;
  } else {
    window_start = stack_start >= window_dur ? stack_start - window_dur : 0;
  }

  GESSource *parent_source = timeline_get_parent_uri_source (self->timeline);
  if (parent_source) {
    /* TODO: time-effect: Add support for time effects! */
    window_start =
        MAX (window_start, GES_TIMELINE_ELEMENT_INPOINT (parent_source));
    window_dur =
        MIN (window_dur, GES_TIMELINE_ELEMENT_DURATION (parent_source));
    GST_DEBUG_OBJECT (self->timeline,
        "Reducing window %" GST_PTR_FORMAT "[%" GST_TIMEP_FORMAT "- %"
        GST_TIMEP_FORMAT "]", parent_source, &window_start, &window_dur);
  }
  g_clear_object (&parent_source);

  for (gint i = 0; i < self->pooled_sources->len; i++) {
    PooledSource *source =
        &g_array_index (self->pooled_sources, PooledSource, i);

    if (!entered_window) {
      if (source->start >= window_start) {
        entered_window = TRUE;
      } else {
        continue;
      }
    }

    if (source->start >= stack_start && source->end <= stack_end) {
      /* Do not reload sources that are currently running */
      continue;
    }

    if (source->start > window_stop) {
      break;
    }

    if (source->track != track) {
      continue;
    }

    GObject *decoderpipeline = FALSE;
    GST_DEBUG_OBJECT (self->timeline,
        "Preparing %s [%" GST_TIMEP_FORMAT "- %" GST_TIMEP_FORMAT "]",
        GST_OBJECT_NAME (source->element), &source->start, &source->end);
    g_signal_emit_by_name (self->pool, "prepare-pipeline", source->element,
        &decoderpipeline);
    if (decoderpipeline) {
      gst_object_ref (source->element);
      g_array_append_val (self->prepared_sources, *source);
      PooledSource *prepared =
          &g_array_index (self->prepared_sources, PooledSource,
          self->prepared_sources->len - 1);
      prepared->decoderpipe = decoderpipeline;
    }

    if (self->prepared_sources->len >= max_preloaded_sources) {
      GST_INFO_OBJECT (self->timeline, "%d sources prepared already.",
          max_preloaded_sources);
#ifndef GST_DISABLE_GST_DEBUG
      if (gst_debug_category_get_threshold (GST_CAT_DEFAULT) >= GST_LEVEL_DEBUG) {
        for (gint i = 0; i < self->prepared_sources->len; i++) {
          PooledSource *source =
              &g_array_index (self->prepared_sources, PooledSource, i);
          GST_DEBUG_OBJECT (self->timeline,
              "Prepared: %s [%" GST_TIMEP_FORMAT "- %" GST_TIMEP_FORMAT "]",
              GST_OBJECT_NAME (source->element), &source->start, &source->end);
        }
      }
#endif
      break;
    }
  }

  GPtrArray *to_remove = g_ptr_array_sized_new (self->prepared_sources->len);
  g_ptr_array_set_free_func (to_remove, (GDestroyNotify) gst_object_unref);
  for (gint i = 0; i < self->prepared_sources->len; i++) {
    PooledSource *source =
        &g_array_index (self->prepared_sources, PooledSource, i);

    if (source->track != track)
      continue;

    gboolean in_window = (source->start >= window_start)
        || (source->start > window_stop);
    if (!in_window) {
      GST_DEBUG_OBJECT (self->timeline,
          "Unpreparing pipeline for %s [%" GST_TIMEP_FORMAT "- %"
          GST_TIMEP_FORMAT "]", GST_OBJECT_NAME (source->element),
          &source->start, &source->end);
      g_ptr_array_add (to_remove, gst_object_ref (source->element));
      GST_ERROR_OBJECT (self->timeline, "Unprepared %s",
          GST_OBJECT_NAME (source->element));
    }
  }

  GST_DEBUG_OBJECT (self->timeline, "%d sources prepared",
      self->prepared_sources->len);
  g_rec_mutex_unlock (&self->lock);

  for (guint i = 0; i < to_remove->len; i++) {
    GstElement *element = g_ptr_array_index (to_remove, i);
    gboolean res;
    g_signal_emit_by_name (self->pool, "unprepare-pipeline", element, &res);
    GST_LOG_OBJECT (self->timeline, "Unprepared %s: result %s",
        GST_OBJECT_NAME (element), res ? "TRUE" : "FALSE");
  }

  g_ptr_array_unref (to_remove);
}

void
ges_pipeline_pool_manager_unprepare_all (GESPipelinePoolManager * self)
{
  g_rec_mutex_lock (&self->lock);
  if (!self->prepared_sources)
    goto done;

  for (gint i = 0; i < self->prepared_sources->len; i++) {
    PooledSource *source =
        &g_array_index (self->prepared_sources, PooledSource, i);
    gboolean res;
    g_signal_emit_by_name (self->pool, "unprepare-pipeline", source->element,
        &res);
  }
done:
  g_rec_mutex_unlock (&self->lock);
}

/* With self->lock taken */
static gboolean
list_pooled_sources (GNode * node, GESPipelinePoolManager * self)
{
  if (GES_IS_AUDIO_URI_SOURCE (node->data)
      || GES_IS_VIDEO_URI_SOURCE (node->data)) {
    gboolean is_nested_timeline;

    g_object_get (ges_extractable_get_asset (GES_EXTRACTABLE
            (GES_TIMELINE_ELEMENT_PARENT (node->data))), "is-nested-timeline",
        &is_nested_timeline, NULL);

    if (is_nested_timeline) {
      GST_INFO_OBJECT (self->timeline, "Ignoring nested timeline");
      self->has_subtimelines = TRUE;
      return TRUE;
    }

    GstElement *source_element =
        ges_source_get_source_element (GES_SOURCE (node->data));
    if (!g_strcmp0 (GST_OBJECT_NAME (gst_element_get_factory (source_element)),
            "uridecodepoolsrc")) {
      PooledSource s = {
        .element = gst_object_ref (source_element),
        .track = ges_track_element_get_track (node->data),
        .start = GES_TIMELINE_ELEMENT_START (node->data),
        .end = GES_TIMELINE_ELEMENT_END (node->data),
      };

      g_array_append_val (self->pooled_sources, s);
    }
  }

  g_array_sort (self->pooled_sources, (GCompareFunc) compare_pooled_source);

  return FALSE;
}

void
ges_pipeline_pool_manager_set_rendering (GESPipelinePoolManager * self,
    gboolean rendering)
{
  GST_DEBUG_OBJECT (self->timeline, "Set rendering %d", rendering);

  g_rec_mutex_lock (&self->lock);
  self->rendering = rendering;
  g_rec_mutex_unlock (&self->lock);
}

static void
ges_pipeline_pool_manager_prepare_pipeline_removed_cb (GObject * pool,
    GstElement * src, GObject * decoderpipe, GESPipelinePoolManager * self)
{
  g_rec_mutex_lock (&self->lock);
  if (!self->prepared_sources) {
    g_rec_mutex_unlock (&self->lock);

    return;
  }

  for (gint i = 0; i < self->prepared_sources->len; i++) {
    PooledSource *source =
        &g_array_index (self->prepared_sources, PooledSource, i);
    if (source->decoderpipe == decoderpipe) {
      gst_object_unref (source->decoderpipe);
      GST_DEBUG_OBJECT (self->timeline, "Removing prepared source %s",
          GST_OBJECT_NAME (src));
      g_array_remove_index (self->prepared_sources, i);
      break;
    }
  }
  g_rec_mutex_unlock (&self->lock);
}

void
ges_pipeline_pool_manager_commit (GESPipelinePoolManager * self)
{
  g_rec_mutex_lock (&self->lock);
  if (!self->pooled_sources)
    goto done;

  GNode *tree = timeline_get_tree (self->timeline);
  g_array_remove_range (self->pooled_sources, 0, self->pooled_sources->len);
  g_node_traverse (tree, G_IN_ORDER, G_TRAVERSE_LEAVES, -1,
      (GNodeTraverseFunc) list_pooled_sources, self);
  if (self->has_subtimelines)
    g_array_remove_range (self->pooled_sources, 0, self->pooled_sources->len);
  self->has_subtimelines = FALSE;

done:
  g_rec_mutex_unlock (&self->lock);
}

static void
deep_element_added_cb (GstBin * _pipeline, GstBin * _sub_bin,
    GstElement * element)
{
  if (!GST_IS_VIDEO_DECODER (element)) {
    return;
  }

  GST_DEBUG_OBJECT (element, "Output out of segment frames!");
  g_object_set (element, "output-out-of-segment", TRUE, NULL);
}


static void
new_pipeline_cb (GObject * pool, GstElement * pipeline)
{
  GST_DEBUG_OBJECT (pipeline, "Connecting %" GST_PTR_FORMAT, pool);
  g_signal_connect (pipeline, "deep-element-added",
      G_CALLBACK (deep_element_added_cb), NULL);
}

void
ges_pipeline_pool_clear (GESPipelinePoolManager * self)
{
  g_rec_mutex_lock (&self->lock);
  if (self->pooled_sources) {
    g_array_free (self->pooled_sources, TRUE);
    g_array_free (self->prepared_sources, TRUE);
    self->prepared_sources = NULL;
    self->pooled_sources = NULL;
  }

  if (self->pool) {
    g_signal_handlers_disconnect_by_func (self->pool,
        G_CALLBACK (ges_pipeline_pool_manager_prepare_pipeline_removed_cb), self);
    g_signal_handlers_disconnect_by_func (self->pool,
        G_CALLBACK (new_pipeline_cb), NULL);
  }

  gst_clear_object (&self->pool);
  g_rec_mutex_unlock (&self->lock);
}

void
ges_pipeline_pool_manager_init (GESPipelinePoolManager * self,
    GESTimeline * timeline)
{
  static gsize init = 0;

  if (g_once_init_enter ((gsize *) & init)) {
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "gespipelinepoolmanager", 0,
        "gespipelinepoolmanager");

    g_once_init_leave ((gsize *) & init, 1);
  }

  GstElement *uridecodepoolsrc =
      gst_element_factory_make ("uridecodepoolsrc", NULL);

  g_rec_mutex_init (&self->lock);
  if (!uridecodepoolsrc)
    return;

  self->timeline = timeline;
  self->pooled_sources = g_array_new (100, TRUE, sizeof (PooledSource));
  g_array_set_clear_func (self->pooled_sources,
      (GDestroyNotify) pooled_source_clear);
  self->prepared_sources = g_array_new (100, TRUE, sizeof (PooledSource));
  g_array_set_clear_func (self->prepared_sources,
      (GDestroyNotify) pooled_source_clear);
  self->pool =
      gst_child_proxy_get_child_by_name (GST_CHILD_PROXY (uridecodepoolsrc),
      "pool");
  g_object_set (self->pool, "cleanup-timeout", 0, NULL);
  g_signal_connect (self->pool, "prepared-pipeline-removed",
      G_CALLBACK (ges_pipeline_pool_manager_prepare_pipeline_removed_cb), self);
  g_signal_connect (self->pool, "new-pipeline", G_CALLBACK (new_pipeline_cb),
      NULL);
  gst_object_unref (uridecodepoolsrc);
}
