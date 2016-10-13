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

#ifndef GST_TRACER_CTF_RECORD_H
#define GST_TRACER_CTF_RECORD_H

#include <glib-object.h>
#include "gsttracerrecord.h"

G_BEGIN_DECLS

#define GST_TYPE_TRACER_CTF_RECORD (gst_tracer_ctf_record_get_type())
#define GST_IS_TRACER_CTF_RECORD(obj)             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_TRACER_CTF_RECORD))
#define GST_IS_TRACER_CTF_RECORD_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_TRACER_CTF_RECORD))
#define GST_TRACER_CTF_RECORD_GET_CLASS(obj)      (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_TRACER_CTF_RECORD, GstTracerCtfRecordClass))
#define GST_TRACER_CTF_RECORD(obj)                (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_TRACER_CTF_RECORD, GstTracerCtfRecord))
#define GST_TRACER_CTF_RECORD_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_TRACER_CTF_RECORD, GstTracerCtfRecordClass))
#define GST_TRACER_CTF_RECORD_CAST(obj)           ((GstTracerCtfRecord*)(obj))

typedef struct _GstTracerCtfRecord GstTracerCtfRecord;
typedef struct _GstTracerCtfRecordClass GstTracerCtfRecordClass;

GType          gst_tracer_ctf_record_get_type   (void);
void           gst_ctf_init_pre   (void);
void           gst_ctf_init_post  (void);
void           gst_ctf_deinit     (void);
gboolean       gst_use_ctf        (void);

G_END_DECLS

#endif /* GST_TRACER_CTF_RECORD_H */
