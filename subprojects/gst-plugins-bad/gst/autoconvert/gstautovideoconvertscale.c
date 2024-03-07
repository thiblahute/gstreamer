/* GStreamer
 * Copyright 2024 Igalia S.L.
 *  @author: Thibault Saunier <tsaunier@igalia.com>
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
 * SECTION:element-autovideoconvertscale
 * @title: autovideoconvertscale
 *
 * The #autovideoconvertscale element is used to convert video frames between
 * different color spaces and scales the video to the requested size.
 *
 * Since: 1.24
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstautovideoconvertscale.h"
#include "gstautovideo.h"

GST_DEBUG_CATEGORY (autovideoconvertscale_debug);
#define GST_CAT_DEFAULT (autovideoconvertscale_debug)

struct _GstAutoVideoConvertScale
{
  GstBaseAutoConvert parent;
};

G_DEFINE_TYPE (GstAutoVideoConvertScale, gst_auto_video_convert_scale,
    GST_TYPE_BASE_AUTO_CONVERT);

GST_ELEMENT_REGISTER_DEFINE (autovideoconvertscale, "autovideoconvertscale",
    GST_RANK_NONE, gst_auto_video_convert_scale_get_type ());

static void
gst_auto_video_convert_scale_class_init (GstAutoVideoConvertScaleClass * klass)
{
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  GST_DEBUG_CATEGORY_INIT (autovideoconvertscale_debug, "autovideoconvertscale",
      0, "Auto color space converter and scaler");

  gst_element_class_set_static_metadata (gstelement_class,
      "Select color space converter and scalers based on caps",
      "Bin/Colorspace/Scale/Video/Converter",
      "Selects the right color space converter based on the caps",
      "Thibault Saunier <tsaunier@igalia.com>");
}

static void
gst_auto_video_convert_scale_init (GstAutoVideoConvertScale *
    autovideoconvertscale)
{
  /* *INDENT-OFF* */
  static const GstAutoVideoFilterGenerator gen[] = {
    {
      .first_elements = { "bayer2rgb", NULL},
      .colorspace_converters = { "videoconvertscale", NULL },
      .last_elements = { NULL } ,
      .filters = {  NULL},
      .rank = GST_RANK_SECONDARY,
    },
    {
      .first_elements = { "capsfilter caps=\"video/x-raw\"", NULL, },
      .colorspace_converters = { "videoconvertscale", NULL },
      .last_elements = { "rgb2bayer", NULL },
      .filters = {  NULL },
      .rank = GST_RANK_SECONDARY,
    },
    {
      .first_elements = { "capsfilter caps=\"video/x-raw\"", NULL, },
      .colorspace_converters = { "videoconvertscale", NULL },
      .last_elements = { NULL, },
      .filters = { NULL },
      .rank = GST_RANK_SECONDARY,
    },
    {
      .first_elements = { NULL, },
      .colorspace_converters = { "glcolorconvert", "glcolorscale", "glcolorconvert", NULL },
      .last_elements = { NULL, },
      .filters = { NULL },
      .rank = GST_RANK_PRIMARY,
    },
    {
      .first_elements = { "glupload", },
      .colorspace_converters = { "glcolorconvert", "glcolorscale", "glcolorconvert", NULL },
      .last_elements = { NULL, },
      .filters = { NULL },
      .rank = GST_RANK_PRIMARY,
    },
    {
      .first_elements = { "capsfilter caps=\"video/x-raw\"", "videoconvertscale", "glupload", NULL },
      .colorspace_converters = { NULL },
      .last_elements = { NULL },
      .filters = { NULL },
      .rank = GST_RANK_MARGINAL + 1,
    },
    {
      .first_elements = { "glcolorconvert", "gldownload", NULL },
      .colorspace_converters = { NULL },
      .last_elements = { NULL },
      .filters = { NULL },
      .rank = GST_RANK_MARGINAL + 2,
    },
    { /* Worst case we upload/download as required */
      .first_elements = { "glupload", "gldownload", NULL },
      .colorspace_converters = { "glcolorconvert",  "glcolorscale", "glcolorconvert", NULL },
      .last_elements = { "glupload", "gldownload", NULL },
      .filters = { NULL },
      .rank = GST_RANK_MARGINAL + 1,
    },
    { /* Pure cuda is preferred */
      .first_elements = { NULL },
      .colorspace_converters = { "cudaconvertscale", NULL },
      .last_elements = { NULL },
      .filters = { NULL },
      .rank = GST_RANK_PRIMARY,
    },
    { /* FIXME: Generically make it so we go through cudaconvert for formats not supported by `glcolorconvert` */
      .first_elements = { "capsfilter caps=video/x-raw(ANY),format={I420_10LE,I422_10LE,I422_12LE}", "cudaupload", NULL },
      .colorspace_converters = { "cudaconvertscale", NULL },
      .last_elements = { "cudadownload", "capsfilter caps=video/x-raw(memory:GLMemory),format={ARGB,RGBA}", NULL },
      .filters = { NULL },
      .rank = GST_RANK_SECONDARY + 2,
    },
    { /* CUDA -> GL */
      .first_elements = { "capsfilter caps=video/x-raw(memory:CUDAMemory)", "cudadownload", NULL },
      .colorspace_converters = { "glcolorconvert",  "glcolorscale", "glcolorconvert", NULL },
      .last_elements = { "glupload", "gldownload", NULL },
      .filters = { NULL },
      .rank = GST_RANK_SECONDARY,
    },
    { /* GL memory to cuda */
      .first_elements = { NULL },
      .colorspace_converters = { "glcolorconvert",  "glcolorscale", "glcolorconvert", NULL },
      .last_elements = { "cudaupload", "capsfilter caps=video/x-raw(memory:CUDAMemory)", NULL },
      .filters = { NULL },
      .rank = GST_RANK_MARGINAL,
    },
    { /* System memory to cuda */
      .first_elements = { "capsfilter caps=\"video/x-raw\"", NULL },
      .colorspace_converters = { "videoconvertscale", NULL },
      .last_elements = { "cudaupload", "capsfilter caps=video/x-raw(memory:CUDAMemory)", NULL },
      .filters = { NULL },
      .rank = GST_RANK_MARGINAL,
    },
    {
      .first_elements = { NULL, },
      .colorspace_converters = { "d3d11convert", NULL },
      .last_elements = { NULL, },
      .filters = { NULL },
      .rank = GST_RANK_PRIMARY,
    },
    {
      .first_elements = { "d3d11download", "d3d11upload", NULL},
      .colorspace_converters = { "glcolorconvert", "glcolorscale", "glcolorconvert", NULL },
      .last_elements = { "d3d11download", "d3d11upload", NULL },
      .filters = { NULL },
      .rank = GST_RANK_MARGINAL,
    },
    { /* Worst case we upload/download as required */
      .first_elements = { NULL},
      .colorspace_converters = { NULL },
      .last_elements = { NULL },
      .filters = { NULL },
      .rank = 0,
    },
  };
  /* *INDENT-ON* */


  gst_auto_video_register_well_known_bins (GST_BASE_AUTO_CONVERT
      (autovideoconvertscale), gen);
}
