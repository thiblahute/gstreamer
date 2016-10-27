#if !defined(INFO_TRACEPOINT_PROVIDER) || defined(TRACEPOINT_HEADER_MULTI_READ)
#undef TRACEPOINT_PROVIDER
#define TRACEPOINT_PROVIDER gst_info

#undef TRACEPOINT_INCLUDE
#define TRACEPOINT_INCLUDE "gstinfotp.h"
#define INFO_TRACEPOINT_PROVIDER

#include <lttng/tracepoint.h>
#include <gst/gst.h>

TRACEPOINT_EVENT(
        gst_info,
        debug,
        TP_ARGS(
            gchar*, category,
            GstDebugLevel, level,
            const gchar *, file,
            const gchar *, function,
            gint, line,
            gchar *, object,
            const gchar *, message),

        TP_FIELDS(
            ctf_string (category, category)
            ctf_integer (gint64, level, level)
            ctf_string (file, file)
            ctf_string (function, function)
            ctf_integer (gint, line, line)
            ctf_string (object, object)
            ctf_string (message, message)
            )
        )
#include <lttng/tracepoint-event.h>

#endif /* INFO_TRACEPOINT_PROVIDER */

