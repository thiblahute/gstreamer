#include "ges-pipeline-pool-manager.h"
#include "ges-internal.h"


#undef GST_CAT_DEFAULT
#define GST_CAT_DEFAULT ges_pipeline_pool_manager_debug

/* FIXME Make that a public setting */
#define MAX_PRELOADED_SOURCES 4

GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

typedef struct
{
  GstElement *element;

  /* Not keeping ref as it is only used to do pointer comparison */
  GESTrack *track;

  GstClockTime start;
  GstClockTime end;
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

  g_mutex_lock (&self->lock);
  if (!self->pooled_sources)
    goto done;

  GST_LOG_OBJECT (self->timeline, "We are rendering: %d", self->rendering);
  if (self->rendering) {
    window_start = stack_start;
    max_preloaded_sources = max_preloaded_sources / 2;
  } else {
    window_start = stack_start >= window_dur ? stack_start - window_dur : 0;
  }

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

    if (source->start > window_stop) {
      break;
    }

    if (source->track != track)
      continue;

    gboolean res = TRUE;
    GST_LOG_OBJECT (self->timeline,
        "Preparing %s [%" GST_TIMEP_FORMAT "- %" GST_TIMEP_FORMAT "]",
        GST_OBJECT_NAME (source->element), &source->start, &source->end);
    g_signal_emit_by_name (self->pool, "prepare-pipeline", source->element,
        &res);
    if (res) {
      gst_object_ref (source->element);
      g_array_append_val (self->prepared_sources, *source);
    }

    if (self->prepared_sources->len >= max_preloaded_sources) {
      GST_INFO_OBJECT (self->timeline, "%d sources prepared already.",
          max_preloaded_sources);
      break;
    }
  }

  GArray *unprepare_source_indexes =
      g_array_new (self->prepared_sources->len, FALSE, sizeof (gint));
  for (gint i = 0; i < self->prepared_sources->len; i++) {
    PooledSource *source =
        &g_array_index (self->prepared_sources, PooledSource, i);

    if (source->track != track)
      continue;

    gboolean in_window = (source->start >= window_start)
        || (source->start > window_stop);
    if (!in_window) {
      gboolean res;
      GST_LOG_OBJECT (self->timeline,
          "Unpreparing pipeline for %s [%" GST_TIMEP_FORMAT "- %"
          GST_TIMEP_FORMAT "]", GST_OBJECT_NAME (source->element),
          &source->start, &source->end);
      g_signal_emit_by_name (self->pool, "unprepare-pipeline", source->element,
          &res);
      g_array_append_val (unprepare_source_indexes, i);
    }
  }

  for (gint i = unprepare_source_indexes->len - 1; i >= 0; i--) {
    g_array_remove_index_fast (self->prepared_sources,
        unprepare_source_indexes->data[i]);
  }

done:
  g_mutex_unlock (&self->lock);
}

void
ges_pipeline_pool_manager_unprepare_all (GESPipelinePoolManager * self)
{
  g_mutex_lock (&self->lock);
  if (!self->prepared_sources)
    goto done;

  for (gint i = 0; i < self->prepared_sources->len; i++) {
    PooledSource *source =
        &g_array_index (self->prepared_sources, PooledSource, i);
    gboolean res;
    g_signal_emit_by_name (self->pool, "unprepare-pipeline", source->element,
        &res);
  }
  g_array_remove_range (self->prepared_sources, 0, self->prepared_sources->len);
done:
  g_mutex_unlock (&self->lock);
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
ges_pipeline_pool_clear (GESPipelinePoolManager * self)
{
  g_mutex_lock (&self->lock);
  if (self->pooled_sources) {
    g_array_free (self->pooled_sources, TRUE);
    g_array_free (self->prepared_sources, TRUE);
  }
  g_mutex_unlock (&self->lock);

  gst_object_unref (self->pool);
}

void
ges_pipeline_pool_manager_set_rendering (GESPipelinePoolManager * self,
    gboolean rendering)
{
  GST_LOG_OBJECT (self->timeline, "Set rendering %d", rendering);

  g_mutex_lock (&self->lock);
  self->rendering = rendering;
  g_mutex_unlock (&self->lock);
}

void
ges_pipeline_pool_manager_commit (GESPipelinePoolManager * self)
{
  g_mutex_lock (&self->lock);
  if (!self->pooled_sources)
    goto done;

  GNode *tree = timeline_get_tree (self->timeline);
  self->has_subtimelines = FALSE;
  g_array_remove_range (self->pooled_sources, 0, self->pooled_sources->len);
  g_node_traverse (tree, G_IN_ORDER, G_TRAVERSE_LEAVES, -1,
      (GNodeTraverseFunc) list_pooled_sources, self);
  if (self->has_subtimelines)
    g_array_remove_range (self->pooled_sources, 0, self->pooled_sources->len);

done:
  g_mutex_unlock (&self->lock);
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
}
