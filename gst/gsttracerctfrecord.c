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
#include "gsttracerctfrecord.h"
#include <babeltrace/ctf-writer/writer.h>
#include <babeltrace/ctf-writer/clock.h>
#include <babeltrace/ctf-writer/stream.h>
#include <babeltrace/ctf-writer/event.h>
#include <babeltrace/ctf-writer/event-types.h>
#include <babeltrace/ctf-writer/event-fields.h>
#include <babeltrace/ctf-writer/stream-class.h>

#define STREAM_FLUSH_COUNT 1000

static struct bt_ctf_writer *writer = NULL;
static GHashTable *streams = NULL;
struct bt_ctf_clock *ct_clock = NULL;

struct _GstTracerCtfRecordClass
{
  GstTracerRecordClass parent;
};

typedef struct
{
  struct bt_ctf_stream *ctfstream;
  gint n_logs;
} CtfFlushStream;

struct _GstTracerCtfRecord
{
  GstTracerRecord parent;
  struct bt_ctf_event_class *event_class;
  CtfFlushStream *stream;
};

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

static void
free_stream (CtfFlushStream * stream)
{
  bt_ctf_stream_flush (stream->ctfstream);
  bt_put (stream->ctfstream);

  g_free (stream);
}

static struct bt_ctf_field_type *
gst_tracer_ctf_create_type_for_gtype (GstTracerCtfRecord * self, GType type)
{
  gint i;
  struct bt_ctf_field_type *enum_type, *int_64_type =
      bt_ctf_field_type_integer_create (64);

  bt_ctf_field_type_integer_set_signed (int_64_type, TRUE);
  enum_type = bt_ctf_field_type_enumeration_create (int_64_type);

  if (g_type_is_a (type, G_TYPE_ENUM)) {
    GEnumClass *eclass = g_type_class_peek (type);

    for (i = 0; eclass->values[i].value_name; i++) {
      bt_ctf_field_type_enumeration_add_mapping (enum_type,
          eclass->values[i].value_name, eclass->values[i].value,
          eclass->values[i].value);
    }
  } else if (g_type_is_a (type, G_TYPE_FLAGS)) {
    GFlagsClass *fclass = g_type_class_peek (type);

    for (i = 0; fclass->values[i].value_name; i++) {
      bt_ctf_field_type_enumeration_add_mapping (enum_type,
          fclass->values[i].value_name, fclass->values[i].value,
          fclass->values[i].value);
    }
  }
  bt_put (int_64_type);

  return enum_type;
}

static gboolean
gst_tracer_ctf_record_add_event_class_field (GQuark field_id,
    const GValue * value, GstTracerCtfRecord * self)
{
  struct bt_ctf_field_type *type = NULL;
  gchar *cleaned_field_name;

  switch (G_VALUE_TYPE (value)) {
    case G_TYPE_STRING:
      type = bt_ctf_field_type_string_create ();
      break;
    case G_TYPE_INT:
      type = bt_ctf_field_type_integer_create (sizeof (gint) * 8);
      bt_ctf_field_type_integer_set_signed (type, TRUE);
      break;
    case G_TYPE_UINT:
      type = bt_ctf_field_type_integer_create (sizeof (guint) * 8);
      break;
    case G_TYPE_BOOLEAN:
      type = bt_ctf_field_type_integer_create (8);
      break;
    case G_TYPE_INT64:
      type = bt_ctf_field_type_integer_create (64);
      bt_ctf_field_type_integer_set_signed (type, TRUE);
      break;
    case G_TYPE_POINTER:
    case G_TYPE_UINT64:
      type = bt_ctf_field_type_integer_create (64);
      break;
    case G_TYPE_FLOAT:
      type = bt_ctf_field_type_floating_point_create ();
      bt_ctf_field_type_floating_point_set_exponent_digits (type, FLT_EXP_DIG);
      bt_ctf_field_type_floating_point_set_mantissa_digits (type, FLT_MANT_DIG);
      break;
    case G_TYPE_DOUBLE:
      type = bt_ctf_field_type_floating_point_create ();
      bt_ctf_field_type_floating_point_set_exponent_digits (type, DBL_EXP_DIG);
      bt_ctf_field_type_floating_point_set_mantissa_digits (type, DBL_MANT_DIG);
      break;
    default:
      if (g_type_is_a (G_VALUE_TYPE (value), G_TYPE_ENUM) ||
          g_type_is_a (G_VALUE_TYPE (value), G_TYPE_FLAGS)) {
        type = g_type_get_qdata (G_VALUE_TYPE (value),
            g_quark_from_static_string ("bt_ctf_type"));

        if (!type) {
          type =
              gst_tracer_ctf_create_type_for_gtype (self, G_VALUE_TYPE (value));

          g_type_set_qdata (G_VALUE_TYPE (value),
              g_quark_from_static_string ("bt_ctf_type"), type);
        }
      } else if (g_type_is_a (G_VALUE_TYPE (value), GST_TYPE_STRUCTURE)) {
        struct bt_ctf_field_type *str_type = bt_ctf_field_type_string_create ();

        type = bt_ctf_field_type_structure_create ();
        bt_ctf_field_type_structure_add_field (type, str_type,
            "gststructure_string");

        bt_put (str_type);
        break;
      }
      break;
  }

  if (type) {
    gchar *c;
    const gchar *f, *field_name = g_quark_to_string (field_id);

    c = cleaned_field_name = g_strdup (field_name);

    for (f = field_name; *f != '\0'; f++, c++) {
      if (!g_ascii_isalnum (*f))
        *c = '_';
      else
        *c = *f;
    }

    if (bt_ctf_event_class_add_field (self->event_class, type,
            cleaned_field_name)) {
      GST_ERROR ("Could not add field: '%s'", cleaned_field_name);
      g_assert_not_reached ();
    }
    bt_put (type);
  } else {
    g_error ("Unhandled type: %s!\n", G_VALUE_TYPE_NAME (value));
  }

  return TRUE;
}

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
    return FALSE;
  }

  sub = gst_value_get_structure (value);
  gst_structure_get (sub, "type", G_TYPE_GTYPE, &type, "flags",
      GST_TYPE_TRACER_VALUE_FLAGS, &flags, NULL);

  if (flags & GST_TRACER_VALUE_FLAGS_OPTIONAL) {
    gchar *opt_name = g_strconcat ("have-", g_quark_to_string (field_id), NULL);

    /* add a boolean field, that indicates the presence of the next field */
    g_value_init (&template_value, G_TYPE_BOOLEAN);
    gst_tracer_ctf_record_add_event_class_field (g_quark_from_string
        (opt_name), &template_value, self);
    g_value_unset (&template_value);
    g_free (opt_name);
  }

  g_value_init (&template_value, type);
  res = gst_tracer_ctf_record_add_event_class_field (field_id,
      &template_value, self);
  g_value_unset (&template_value);

  return res;
}

static gboolean
gst_tracer_ctf_record_build_format (GstTracerRecord * record,
    GstStructure * structure)
{
  GstTracerCtfRecord *self = GST_TRACER_CTF_RECORD (record);
  struct bt_ctf_stream_class *stream_class;
  CtfFlushStream *fstream;

  fstream = g_hash_table_lookup (streams, record->source_name);
  if (!fstream) {
    struct bt_ctf_stream *stream;

    stream_class = bt_ctf_stream_class_create (record->source_name);
    bt_ctf_stream_class_set_clock (stream_class, ct_clock);
    stream = bt_ctf_writer_create_stream (writer, stream_class);
    g_assert (stream);

    fstream = g_new0 (CtfFlushStream, 1);
    fstream->ctfstream = stream;

    g_hash_table_insert (streams, g_strdup (record->source_name), fstream);
  } else {
    stream_class = bt_ctf_stream_get_class (fstream->ctfstream);
  }

  self->stream = fstream;
  self->event_class =
      bt_ctf_event_class_create (gst_structure_get_name (structure));
  gst_structure_foreach (structure,
      (GstStructureForeachFunc) gst_tracer_ctf_add_event_class, self);
  g_assert (!bt_ctf_stream_class_add_event_class (stream_class,
          self->event_class));

  bt_put (stream_class);

  return TRUE;
}

static void
gst_tracer_ctf_record_append_event (GstTracerCtfRecord * self,
    struct bt_ctf_event *event)
{
  g_assert (!bt_ctf_stream_append_event (self->stream->ctfstream, event));
  bt_put (event);

  /* Not really MT safe... but it should not cause any issue! */
  self->stream->n_logs++;
  if (self->stream->n_logs > STREAM_FLUSH_COUNT) {
    self->stream->n_logs = 0;
    bt_ctf_stream_flush (self->stream->ctfstream);
  }
}

static void
gst_tracer_ctf_record_log (GstTracerRecord * record, va_list var_args)
{
  gint i;
  struct bt_ctf_field *field;
  struct bt_ctf_field_type *field_type;
  GstTracerCtfRecord *self = GST_TRACER_CTF_RECORD (record);
  struct bt_ctf_event *event = bt_ctf_event_create (self->event_class);

  for (i = 0; (field = bt_ctf_event_get_payload_by_index (event, i)); i++) {
    field_type = bt_ctf_field_get_type (field);
    switch (bt_ctf_field_type_get_type_id (field_type)) {
      case CTF_TYPE_STRING:
        g_assert (!bt_ctf_field_string_set_value (field, va_arg (var_args,
                    gchar *)));
        break;
      case CTF_TYPE_INTEGER:
      {
        if (bt_ctf_field_type_integer_get_signed (field_type)) {

          if (bt_ctf_field_type_integer_get_size (field_type) == 32)
            g_assert (!bt_ctf_field_signed_integer_set_value (field,
                    va_arg (var_args, gint32)));
          else
            g_assert (!bt_ctf_field_signed_integer_set_value (field,
                    va_arg (var_args, gint64)));

        } else {

          if (bt_ctf_field_type_integer_get_size (field_type) == 8)
            g_assert (!bt_ctf_field_unsigned_integer_set_value (field,
                    ! !va_arg (var_args, gboolean)));
          else if (bt_ctf_field_type_integer_get_size (field_type) == 8)
            g_assert (!bt_ctf_field_unsigned_integer_set_value (field,
                    va_arg (var_args, guint32)));
          else
            g_assert (!bt_ctf_field_unsigned_integer_set_value (field,
                    va_arg (var_args, guint64)));

        }

        break;
      }
      case CTF_TYPE_FLOAT:
      {
        bt_ctf_field_floating_point_set_value (field, va_arg (var_args,
                gdouble));
      }
        break;
      case CTF_TYPE_ENUM:
      {
        struct bt_ctf_field *enum_container_field =
            bt_ctf_field_enumeration_get_container (field);
        gint64 val = va_arg (var_args, gint64);

        bt_ctf_field_signed_integer_set_value (enum_container_field, val);

        break;
      }
      case CTF_TYPE_STRUCT:
      {
        GstStructure *structure = va_arg (var_args, GstStructure *);

        gchar *structure_str = gst_structure_to_string (structure);
        struct bt_ctf_field *str_field =
            bt_ctf_field_structure_get_field (field,
            "gststructure_string");

        bt_ctf_field_string_set_value (str_field, structure_str);
        g_free (structure_str);

        break;
      }
      default:
        break;
    }
    bt_put (field_type);
    bt_put (field);
  }

  gst_tracer_ctf_record_append_event (self, event);
}

static void
gst_tracer_ctf_record_finalize (GObject * object)
{
  GstTracerCtfRecord *self = GST_TRACER_CTF_RECORD (object);

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
}

void
gst_ctf_init_pre (void)
{
  const gchar *trace_path;
  const char *format_description = g_getenv ("GST_DEBUG_FORMAT");
  GstStructure *options;

  if (format_description == NULL)
    return;

  options = gst_structure_from_string (format_description, NULL);

  if (!options)
    goto done;

  if (!gst_structure_has_name (options, "ctf"))
    return;

  trace_path = gst_structure_get_string (options, "dir");

  if (!trace_path)
    trace_path = gst_structure_get_string (options, "d");

  if (!trace_path)
    trace_path = gst_structure_get_string (options, "directory");

  if (!trace_path) {
    g_warning ("Could not find a directory where to put"
        " traces in Common Trace Format in the"
        " GST_DEBUG_FORMAT envvar: '%s'.\nYou need to"
        " specify the directory setting the variable in that form:\n"
        " $ GST_DEBUG_FORMAT=ctf,dir=/some/directory\n\n", format_description);
    goto done;
  }

  writer = bt_ctf_writer_create (trace_path);
  ct_clock = bt_ctf_clock_create ("main_clock");
  bt_ctf_writer_add_clock (writer, ct_clock);
  bt_ctf_writer_add_environment_field (writer, "GST_VERSION", PACKAGE_VERSION);

  streams = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, (GDestroyNotify) free_stream);

done:
  if (options)
    gst_structure_free (options);

  return;
}

gboolean
gst_use_ctf (void)
{
  return ! !writer;
}

void
gst_ctf_init_post (void)
{
  bt_ctf_writer_flush_metadata (writer);
}

void
gst_ctf_deinit (void)
{
  if (writer) {
    g_hash_table_unref (streams);
    bt_ctf_writer_flush_metadata (writer);

    BT_PUT (writer);
  }
}
