/* GStreamer
 * Copyright (C) 2019 Thibault Saunier <tsaunier@igalia.com>
 *
 * gstvideoencoderprofilemanager-private.h
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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

#ifndef __GST_VIDEO_ENCODER_BITRATE_PROFILE_MANAGER_PRIVATE_H__
#define __GST_VIDEO_ENCODER_BITRATE_PROFILE_MANAGER_PRIVATE_H__

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

typedef struct _GstVideoEncoderProfileManager GstVideoEncoderProfileManager;

typedef struct _GstVideoEncoderProfileConfigMap
{
  guint n_pixels;
  guint low_framerate_bitrate;
  guint high_framerate_bitrate;

  gpointer _gst_reserved[GST_PADDING_LARGE - 1];
} GstVideoEncoderProfileConfigMap;

G_GNUC_INTERNAL
void
gst_video_encoder_profile_manager_add_profile(GstVideoEncoderProfileManager* self,
    const gchar* profile_name, const GstVideoEncoderProfileConfigMap* map);

G_GNUC_INTERNAL
guint gst_video_encoder_profile_manager_get_bitrate(GstVideoEncoderProfileManager* self, GstVideoInfo* info);

G_GNUC_INTERNAL
void gst_video_encoder_profile_manager_start_loading_preset (GstVideoEncoderProfileManager* self);

G_GNUC_INTERNAL
void gst_video_encoder_profile_manager_end_loading_preset(GstVideoEncoderProfileManager* self, const gchar* preset);

G_GNUC_INTERNAL
gboolean gst_video_encoder_profile_manager_set_bitrate(GstVideoEncoderProfileManager* self, guint bitrate);

G_GNUC_INTERNAL
GstVideoEncoderProfileManager* gst_video_encoder_profile_manager_new(guint default_bitrate);

G_GNUC_INTERNAL
void gst_video_encoder_profile_manager_free(GstVideoEncoderProfileManager* self);

G_END_DECLS

#endif /* __GST_VIDEO_ENCODER_BITRATE_PROFILE_MANAGER_PRIVATE_H__ */
