#!/bin/bash

set -eo pipefail

# Set noninteractive frontend
export DEBIAN_FRONTEND=noninteractive

# Update and upgrade system
apt-get update
apt-get dist-upgrade -y

# Install production dependencies
apt-get install -y --no-install-recommends \
    ca-certificates \
    bubblewrap \
    curl \
    iso-codes \
    libbz2-1.0 \
    libcap2 \
    libcurl3-nss \
    libdrm2 \
    libfontconfig1 \
    libgirepository-1.0-1 \
    libglib2.0-0 \
    libgmp10 \
    libgnutls30 \
    libgsl27 \
    libharfbuzz0b \
    libjpeg8 \
    libmpeg2-4 \
    libopus0 \
    liborc-0.4-0 \
    libpng16-16 \
    libsoup2.4-1 \
    libssl3 \
    libunwind8 \
    libvpx7 \
    libwebp7 \
    libwebpdemux2 \
    libwebpmux3 \
    libx264-163 \
    libx265-199 \
    libfdk-aac2 \
    libzimg2 \
    librsvg2-2 \
    libxml2 \
    shared-mime-info \
    xdg-dbus-proxy \
    zlib1g \
    fontconfig \
    fonts-noto-cjk \
    fonts-noto \
    wget \
    gdb

if [ "${ENABLE_GPU}" = "true" ] || [ "${ENABLE_GPU}" = "1" ]; then
    echo "Installing GPU runtime dependencies..."
    apt-get install -y --no-install-recommends \
        libglvnd0 \
        libgl1 \
        libgles2 \
        libegl1 \
        libudev1 \
        libgudev-1.0-0
else
    echo "Skipping GPU runtime dependencies (ENABLE_GPU != 'true')"
fi

# Clean up
apt-get clean
rm -rf /var/lib/apt/lists/*
