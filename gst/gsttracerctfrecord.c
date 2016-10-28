/* GStreamer
 * Copyright (C) 2016 Thibault Saunier <thibault.saunier@osg.samsung.com>
 *
 * gsttracerctfrecord.c: tracer Common Trace Format record class
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

#ifdef HAVE_CONFIG
#include "config.h"
#endif

#include "gst_private.h"
#include "gstenumtypes.h"
#include "gststructure.h"
#include "gstvalue.h"
#include "gstatomicqueue.h"
#include "gsttracerctfrecord.h"
#include <babeltrace/ctf-writer/writer.h>
#include <babeltrace/ctf-writer/clock.h>
#include <babeltrace/ctf-writer/stream.h>
#include <babeltrace/ctf-writer/event.h>
#include <babeltrace/ctf-writer/event-types.h>
#include <babeltrace/ctf-writer/event-fields.h>
#include <babeltrace/ctf-writer/stream-class.h>

/* FIXME Check if that arbitrary number makes sense ?
 * Maybe make it configurable through env var? */
#define STREAM_FLUSH_COUNT 100
#define CHECK(exp) if (exp) goto failed

static gchar *trace_path = NULL;

/* We concider the CTF writer subsystem
 * initialized when the writing thread is
 * up and the CTF writer has been configured */
static gboolean initialized = FALSE;

/* List of GstTracerCtfRecord to be initialized
 * in the writing thread */
static GList *records = NULL;

/* Queue holding the pending traces */
static GstAtomicQueue *queue = NULL;

static GThread *writing_thread = NULL;
/* The writing lock needs to be hold while signaling
 * the writing cond and while flushing the queue. */
static GMutex writing_lock;
static GCond writing_cond;

struct _GstTracerCtfRecordClass
{
  GstTracerRecordClass parent;
};

struct _GstTracerCtfRecord
{
  GstTracerRecord parent;
  struct bt_ctf_event_class *event_class;
  struct bt_ctf_stream *stream;

  GPtrArray *format;
  GstStructure *definition;
};

typedef struct
{
  GstTracerCtfRecord *record;
  GPtrArray *fields;
} CtfLog;

/* FIXME Copied from writer.py, remove when exposed in the C API */
enum FloatingPointFieldDeclaration
{
  /*IEEE 754 single precision floating point number exponent size */
  FLT_EXP_DIG = 8,

  /*IEEE 754 double precision floating point number exponent size */
  DBL_EXP_DIG = 11
};


/* *INDENT-OFF* */
G_DEFINE_TYPE (GstTracerCtfRecord, gst_tracer_ctf_record,
    GST_TYPE_TRACER_RECORD)
/* *INDENT-ON* */

static struct bt_ctf_field_type *
gst_tracer_ctf_create_type_for_gtype (GstTracerCtfRecord * self, GType type)
{
  gint i;
  struct bt_ctf_field_type *enum_type = NULL, *int_64_type =
      bt_ctf_field_type_integer_create (64);

  CHECK (bt_ctf_field_type_integer_set_signed (int_64_type, TRUE));
  enum_type = bt_ctf_field_type_enumeration_create (int_64_type);

  if (g_type_is_a (type, G_TYPE_ENUM)) {
    GEnumClass *eclass = g_type_class_peek (type);

    for (i = 0; eclass->values[i].value_name; i++) {
      CHECK (bt_ctf_field_type_enumeration_add_mapping (enum_type,
              eclass->values[i].value_name, eclass->values[i].value,
              eclass->values[i].value));
    }
  } else if (g_type_is_a (type, G_TYPE_FLAGS)) {
    GFlagsClass *fclass = g_type_class_peek (type);

    for (i = 0; fclass->values[i].value_name; i++) {
      CHECK (bt_ctf_field_type_enumeration_add_mapping (enum_type,
              fclass->values[i].value_name, fclass->values[i].value,
              fclass->values[i].value));
    }
  }

done:
  bt_put (int_64_type);
  return enum_type;

failed:
  if (enum_type)
    bt_put (enum_type);
  enum_type = NULL;

  goto done;
}

static gboolean
gst_tracer_ctf_record_add_event_class_field (GQuark field_id,
    const GValue * value, GstTracerCtfRecord * self)
{
  struct bt_ctf_field_type *type = NULL;

  switch (G_VALUE_TYPE (value)) {
    case G_TYPE_STRING:
      type = bt_ctf_field_type_string_create ();
      break;
    case G_TYPE_INT:
      type = bt_ctf_field_type_integer_create (sizeof (gint) * 8);
      CHECK (bt_ctf_field_type_integer_set_signed (type, TRUE));
      break;
    case G_TYPE_UINT:
      type = bt_ctf_field_type_integer_create (sizeof (guint) * 8);
      break;
    case G_TYPE_BOOLEAN:
      type = bt_ctf_field_type_integer_create (8);
      break;
    case G_TYPE_INT64:
      type = bt_ctf_field_type_integer_create (64);
      CHECK (bt_ctf_field_type_integer_set_signed (type, TRUE));
      break;
    case G_TYPE_POINTER:
    case G_TYPE_UINT64:
      type = bt_ctf_field_type_integer_create (64);
      break;
    case G_TYPE_FLOAT:
      type = bt_ctf_field_type_floating_point_create ();
      CHECK (bt_ctf_field_type_floating_point_set_exponent_digits (type,
              FLT_EXP_DIG));
      CHECK (bt_ctf_field_type_floating_point_set_mantissa_digits (type,
              FLT_MANT_DIG));
      break;
    case G_TYPE_DOUBLE:
      type = bt_ctf_field_type_floating_point_create ();
      CHECK (bt_ctf_field_type_floating_point_set_exponent_digits (type,
              DBL_EXP_DIG));
      CHECK (bt_ctf_field_type_floating_point_set_mantissa_digits (type,
              DBL_MANT_DIG));
      break;
    default:
      if (g_type_is_a (G_VALUE_TYPE (value), G_TYPE_ENUM) ||
          g_type_is_a (G_VALUE_TYPE (value), G_TYPE_FLAGS)) {
        type = g_type_get_qdata (G_VALUE_TYPE (value),
            g_quark_from_static_string ("bt_ctf_type"));

        if (!type) {
          type =
              gst_tracer_ctf_create_type_for_gtype (self, G_VALUE_TYPE (value));

          if (!type)
            goto failed;

          g_type_set_qdata (G_VALUE_TYPE (value),
              g_quark_from_static_string ("bt_ctf_type"), type);
        }
      } else if (g_type_is_a (G_VALUE_TYPE (value), GST_TYPE_STRUCTURE)) {
        struct bt_ctf_field_type *str_type = bt_ctf_field_type_string_create ();

        type = bt_ctf_field_type_structure_create ();
        CHECK (bt_ctf_field_type_structure_add_field (type, str_type,
                "gststructure_string"));

        bt_put (str_type);
        break;
      }
      break;
  }

  if (type) {
    gboolean res;
    gchar *c, *cleaned_field_name;
    const gchar *f, *field_name = g_quark_to_string (field_id);

    c = cleaned_field_name = g_strdup (field_name);

    for (f = field_name; *f != '\0'; f++, c++) {
      if (!g_ascii_isalnum (*f))
        *c = '_';
      else
        *c = *f;
    }

    res = bt_ctf_event_class_add_field (self->event_class, type,
        cleaned_field_name);
    g_free (cleaned_field_name);
    if (res) {
      GST_ERROR ("Could not add field: '%s'", cleaned_field_name);
      goto failed;
    }

    g_ptr_array_add (self->format, type);
  } else {
    GST_ERROR_OBJECT (self, "Unhandled type: %s!\n", G_VALUE_TYPE_NAME (value));

    goto failed;
  }

  return TRUE;

failed:
  return FALSE;
}

/* NOTE: On any error, self->event_class will be unrefed
 * and set to NULL and the Record will become unusable */
static gboolean
gst_tracer_ctf_add_event_class (GQuark field_id,
    const GValue * value, GstTracerCtfRecord * self)
{
  const GstStructure *sub;
  GValue template_value = { 0, };
  GType type = G_TYPE_INVALID;
  GstTracerValueFlags flags = GST_TRACER_VALUE_FLAGS_NONE;
  gboolean res;

  if (G_VALUE_TYPE (value) != GST_TYPE_STRUCTURE) {
    GST_WARNING ("expected field of type GstStructure, but %s is %s",
        g_quark_to_string (field_id), G_VALUE_TYPE_NAME (value));
    goto failed;
  }

  sub = gst_value_get_structure (value);
  gst_structure_get (sub, "type", G_TYPE_GTYPE, &type, "flags",
      GST_TYPE_TRACER_VALUE_FLAGS, &flags, NULL);

  if (flags & GST_TRACER_VALUE_FLAGS_OPTIONAL) {
    gchar *opt_name = g_strconcat ("have-", g_quark_to_string (field_id), NULL);

    /* add a boolean field, that indicates the presence of the next field */
    g_value_init (&template_value, G_TYPE_BOOLEAN);
    res = gst_tracer_ctf_record_add_event_class_field (g_quark_from_string
        (opt_name), &template_value, self);
    g_value_unset (&template_value);
    g_free (opt_name);

    if (!res)
      goto failed;
  }

  g_value_init (&template_value, type);
  res = gst_tracer_ctf_record_add_event_class_field (field_id,
      &template_value, self);
  g_value_unset (&template_value);

  return res;

failed:
  g_clear_pointer (&self->event_class, bt_put);
  return FALSE;
}

static gboolean
gst_tracer_ctf_record_build_format (GstTracerRecord * record,
    GstStructure * structure)
{
  GST_TRACER_CTF_RECORD (record)->definition = structure;
  records = g_list_prepend (records, record);

  return TRUE;
}

static void
gst_tracer_ctf_record_log (GstTracerRecord * record, va_list var_args)
{
  gint i;
  struct bt_ctf_field_type *field_type;
  CtfLog *log;
  GPtrArray *fields;
  GstTracerCtfRecord *self = GST_TRACER_CTF_RECORD (record);

  if (!initialized) {
    GST_WARNING_OBJECT (record, "Trying to log before the CTF writer is "
        "initialized");

    return;
  }

  if (!self->event_class) {
    GST_INFO_OBJECT (record, "No event class set, probably due to a previous"
        "failure, bailing out");

    return;
  }

  log = g_new (CtfLog, 1);
  log->record = gst_object_ref (record);
  fields = log->fields = g_ptr_array_sized_new (self->format->len);

  for (i = 0; i < self->format->len; i++) {
    field_type = g_ptr_array_index (self->format, i);

    switch (bt_ctf_field_type_get_type_id (field_type)) {
      case CTF_TYPE_STRING:
        g_ptr_array_add (fields, g_strdup (va_arg (var_args, gchar *)));
        break;
      case CTF_TYPE_INTEGER:
      {
        gint64 *i = g_new0 (gint64, 1); /* Just make enough space .... */

        *i = va_arg (var_args, gint64);

        g_ptr_array_add (fields, i);
        break;
      }
      case CTF_TYPE_FLOAT:
      {
        gdouble *i = g_new0 (gdouble, 1);       /* Just make enough space .... */
        *i = va_arg (var_args, gdouble);

        g_ptr_array_add (fields, i);
      }
        break;
      case CTF_TYPE_ENUM:
      {
        gint64 *i = g_new0 (gint64, 1); /* Just make enough space .... */

        *i = va_arg (var_args, gint64);
        g_ptr_array_add (fields, i);
        break;
      }
      case CTF_TYPE_STRUCT:
      {
        GstStructure *structure = va_arg (var_args, GstStructure *);
        gchar *structure_str = gst_structure_to_string (structure);

        g_ptr_array_add (fields, structure_str);
        break;
      }
      default:
        break;
    }
  }

  gst_atomic_queue_push (queue, log);

  /* Not really MT safe... but it should not cause any issue! */
  if (gst_atomic_queue_length (queue) > STREAM_FLUSH_COUNT) {
    g_mutex_lock (&writing_lock);
    if (gst_atomic_queue_length (queue) > STREAM_FLUSH_COUNT)
      g_cond_signal (&writing_cond);
    g_mutex_unlock (&writing_lock);
  }
}

static void
gst_ctf_recorder_write_log (CtfLog * log)
{
  gint i;
  struct bt_ctf_field *field;
  struct bt_ctf_field_type *field_type;
  GstTracerCtfRecord *self = GST_TRACER_CTF_RECORD (log->record);
  struct bt_ctf_event *event = bt_ctf_event_create (self->event_class);

  for (i = 0; (field = bt_ctf_event_get_payload_by_index (event, i)); i++) {
    gpointer *data = g_ptr_array_index (log->fields, i);

    field_type = bt_ctf_field_get_type (field);
    switch (bt_ctf_field_type_get_type_id (field_type)) {
      case CTF_TYPE_STRING:
      {
        CHECK (bt_ctf_field_string_set_value (field, (gchar *) data));

        break;
      }
      case CTF_TYPE_INTEGER:
      {
        if (bt_ctf_field_type_integer_get_signed (field_type)) {
          if (bt_ctf_field_type_integer_get_size (field_type) == 32) {
            CHECK (bt_ctf_field_signed_integer_set_value (field,
                    *((gint32 *) data)));
          } else {
            CHECK (bt_ctf_field_signed_integer_set_value (field,
                    *((gint64 *) data)));
          }
        } else {
          if (bt_ctf_field_type_integer_get_size (field_type) == 8) {
            CHECK (bt_ctf_field_unsigned_integer_set_value (field,
                    *((guint8 *) data)));
          } else if (bt_ctf_field_type_integer_get_size (field_type) == 32) {
            CHECK (bt_ctf_field_unsigned_integer_set_value (field,
                    *((guint32 *) data)));
          } else {
            CHECK (bt_ctf_field_unsigned_integer_set_value (field,
                    *((guint64 *) data)));
          }
        }
        break;
      }
      case CTF_TYPE_FLOAT:
      {
        CHECK (bt_ctf_field_floating_point_set_value (field,
                *((gdouble *) data)));

        break;
      }
      case CTF_TYPE_ENUM:
      {
        struct bt_ctf_field *enum_container_field =
            bt_ctf_field_enumeration_get_container (field);
        gint64 val = (gint64) * data;

        if (bt_ctf_field_signed_integer_set_value (enum_container_field, val))
          goto failed;

        bt_put (enum_container_field);
        break;
      }
      case CTF_TYPE_STRUCT:
      {
        struct bt_ctf_field *str_field =
            bt_ctf_field_structure_get_field (field, "gststructure_string");

        if (bt_ctf_field_string_set_value (str_field, (gchar *) data))
          goto failed;
        break;
      }
      default:
      {
        GST_ERROR_OBJECT (self, "CTF Type %d not handled.",
            bt_ctf_field_type_get_type_id (field_type));

        break;
      }
    }
    g_free (data);
    bt_put (field_type);
    bt_put (field);
    continue;

  failed:
    g_free (data);
    bt_put (field_type);
    bt_put (field);
    goto done;
  }

  if (bt_ctf_stream_append_event (self->stream, event))
    GST_ERROR_OBJECT (self, "Could not add event %p", event);

done:
  bt_put (event);
}

static void
gst_tracer_ctf_record_finalize (GObject * object)
{
  GstTracerCtfRecord *self = GST_TRACER_CTF_RECORD (object);

  if (self->event_class)
    bt_put (self->event_class);
}

static void
gst_tracer_ctf_record_class_init (GstTracerCtfRecordClass * klass)
{
  GstTracerRecordClass *record_klass = GST_TRACER_RECORD_CLASS (klass);
  GObjectClass *object_klass = G_OBJECT_CLASS (klass);

  object_klass->finalize = gst_tracer_ctf_record_finalize;
  record_klass->build_format = gst_tracer_ctf_record_build_format;
  record_klass->log = gst_tracer_ctf_record_log;
}

static void
gst_tracer_ctf_record_init (GstTracerCtfRecord * self)
{
  self->format = g_ptr_array_sized_new (10);
}

static void
gst_ctf_flush_queue (void)
{
  GList *tmp, *streams = NULL;
  CtfLog *log;

  while ((log = (CtfLog *) gst_atomic_queue_pop (queue))) {
    gst_ctf_recorder_write_log (log);

    if (!g_list_find (streams, log->record->stream)) {
      streams = g_list_prepend (streams, log->record->stream);
    }

    g_ptr_array_unref (log->fields);
    gst_object_unref (log->record);
    g_free (log);
  }

  for (tmp = streams; tmp; tmp = tmp->next) {
    GST_INFO ("Flushing %p", tmp->data);
    bt_ctf_stream_flush (tmp->data);
  }

  g_list_free (streams);

}

static gpointer
gst_ctf_write (gpointer unused)
{
  struct bt_ctf_writer *writer = NULL;
  struct bt_ctf_clock *ct_clock = NULL;
  GHashTable *streams = NULL;
  GList *tmp;

  if (!records) {
    GST_INFO ("No record set, not doing anything");

    g_clear_pointer (&writing_thread, g_thread_unref);

    GST_DEBUG ("Nothing to log in CTF ... "
        "waking up the initializing thread");
    g_mutex_lock (&writing_lock);
    initialized = TRUE;
    g_cond_signal (&writing_cond);
    g_mutex_unlock (&writing_lock);

    return NULL;
  }

  writer = bt_ctf_writer_create (trace_path);
  ct_clock = bt_ctf_clock_create ("main_clock");
  bt_ctf_writer_add_clock (writer, ct_clock);
  bt_ctf_writer_add_environment_field (writer, "GST_VERSION", PACKAGE_VERSION);

  streams = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, (GDestroyNotify) bt_put);

  for (tmp = records; tmp; tmp = tmp->next) {
    GstTracerRecord *record = GST_TRACER_RECORD (tmp->data);
    GstTracerCtfRecord *self = GST_TRACER_CTF_RECORD (record);
    GstStructure *structure = self->definition;
    struct bt_ctf_stream_class *stream_class = NULL;
    struct bt_ctf_stream *stream = NULL;

    stream = g_hash_table_lookup (streams, record->source_name);
    if (!stream) {
      stream_class = bt_ctf_stream_class_create (record->source_name);
      bt_ctf_stream_class_set_clock (stream_class, ct_clock);
      stream = bt_ctf_writer_create_stream (writer, stream_class);
      if (!stream) {
        GST_ERROR_OBJECT (record, "Could not create stream, the record"
            " won't be usable.");
        goto next;
      }

      g_hash_table_insert (streams, g_strdup (record->source_name), stream);
    } else {
      stream_class = bt_ctf_stream_get_class (stream);
    }

    self->stream = stream;
    self->event_class =
        bt_ctf_event_class_create (gst_structure_get_name (structure));
    if (!self->event_class) {
      GST_ERROR_OBJECT (self, "Could not create event class, the record"
          " won't be usable");
      goto next;
    }

    gst_structure_foreach (structure,
        (GstStructureForeachFunc) gst_tracer_ctf_add_event_class, self);

    if (bt_ctf_stream_class_add_event_class (stream_class, self->event_class)) {
      GST_ERROR_OBJECT (self, "Could not add event class to the stream");

      bt_put (self->event_class);
      self->event_class = NULL;
      goto next;
    }

  next:
    if (stream_class)
      bt_put (stream_class);
  }
  bt_ctf_writer_flush_metadata (writer);

  g_list_free (records);
  records = NULL;

  /* Declare that thread as up and running */
  g_mutex_lock (&writing_lock);
  initialized = TRUE;
  g_cond_signal (&writing_cond);
  g_mutex_unlock (&writing_lock);

  while (initialized) {
    g_mutex_lock (&writing_lock);
    g_cond_wait (&writing_cond, &writing_lock);
    gst_ctf_flush_queue ();
    g_mutex_unlock (&writing_lock);
  }

  gst_ctf_flush_queue ();

  g_hash_table_unref (streams);
  bt_ctf_writer_flush_metadata (writer);
  BT_PUT (writer);

  return NULL;
}

void
gst_ctf_init_pre (void)
{
  const char *format_description = g_getenv ("GST_DEBUG_FORMAT");
  GstStructure *options;

  if (format_description == NULL)
    return;

  options = gst_structure_from_string (format_description, NULL);

  if (!options)
    goto done;

  if (!gst_structure_has_name (options, "ctf"))
    return;

  trace_path = (gchar *) gst_structure_get_string (options, "dir");
  if (!trace_path)
    trace_path = (gchar *) gst_structure_get_string (options, "d");

  if (!trace_path)
    trace_path = (gchar *) gst_structure_get_string (options, "directory");

  if (!trace_path) {
    gchar *trace_path = g_build_filename (g_get_tmp_dir (),
        "gstctftraces-XXXXXX", NULL);

    g_printerr ("GStreamer CTF logs outputed in: %s\n", trace_path);
  }

  trace_path = g_strdup (trace_path);
  queue = gst_atomic_queue_new (STREAM_FLUSH_COUNT + 10);

done:
  if (options)
    gst_structure_free (options);

  return;
}

void
gst_ctf_init_post (void)
{
  g_cond_init (&writing_cond);
  g_mutex_init (&writing_lock);

  /* All the tracer have been initialized between init_pre and
   * now, we can launch the ctf writing thread and wait for all
   * the Records to be initialized. */
  writing_thread = g_thread_new ("ctf_writer", gst_ctf_write, NULL);
  g_mutex_lock (&writing_lock);
  while (!initialized)
    g_cond_wait (&writing_cond, &writing_lock);
  g_mutex_unlock (&writing_lock);
}

gboolean
gst_use_ctf (void)
{
  return ! !trace_path;
}

void
gst_ctf_deinit (void)
{
  g_mutex_lock (&writing_lock);
  initialized = FALSE;

  if (gst_use_ctf ()) {
    g_cond_signal (&writing_cond);
    g_mutex_unlock (&writing_lock);
    g_thread_join (writing_thread);
  } else {
    g_mutex_unlock (&writing_lock);
  }

  g_cond_clear (&writing_cond);
  g_mutex_clear (&writing_lock);
  g_clear_pointer (&trace_path, g_free);
}
