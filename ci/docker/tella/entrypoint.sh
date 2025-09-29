#!/bin/sh
set -eu

# Require GST_REGISTRY
: "${GST_REGISTRY:?GST_REGISTRY must be set}"

if [ "${GES_CONVERTER_TYPE:-}" = "gl" ]; then
  rm -f -- "$GST_REGISTRY"
  # Rebuild registry and log output to a file
  gst-inspect-1.0 > /var/log/gstreamer-registry-build.log 2>&1
  chmod a+r "$GST_REGISTRY"
fi

# Prevent further registry updates
export GST_REGISTRY_UPDATE=no
export GST_REGISTRY_FORK=no

exec "$@"
