#!/bin/bash

set -eo pipefail

# Set noninteractive frontend
export DEBIAN_FRONTEND=noninteractive

# Update and upgrade system
apt-get update
apt-get dist-upgrade -y

# Reinstall fontconfig-config
apt-get install -y --reinstall --purge fontconfig-config --option=Dpkg::Options::=--force-confdef

# Install main dependencies
apt-get install -y --no-install-recommends \
    bison \
    bubblewrap \
    ca-certificates \
    clang \
    cmake \
    curl \
    flex \
    gcc \
    autoconf \
    automake \
    libtool \
    gettext \
    git \
    gperf \
    iso-codes \
    libbz2-dev \
    libcap-dev \
    libcurl4-nss-dev \
    libdrm-dev \
    libfontconfig-dev \
    libgirepository1.0-dev \
    libglib2.0-dev \
    libgmp-dev \
    libgnutls28-dev \
    libgsl-dev \
    libharfbuzz-dev \
    libjpeg-dev \
    libmpeg2-4-dev \
    libopus-dev \
    liborc-0.4-dev \
    libpng-dev \
    libsoup2.4-dev \
    libssl-dev \
    libunwind-dev \
    libvpx-dev \
    libwebp-dev \
    libwebpdemux2 \
    libwebpmux3 \
    libx264-dev \
    libx265-dev \
    libfdk-aac-dev \
    libzimg-dev \
    librsvg2-dev \
    libxml2-dev \
    nasm \
    shared-mime-info \
    wget \
    xdg-dbus-proxy \
    yasm \
    zlib1g-dev \
    gdb \
    fontconfig \
    alien \
    fonts-noto-cjk \
    fonts-noto

if [ "${ENABLE_GPU}" = "true" ] || [ "${ENABLE_GPU}" = "1" ]; then
    echo "Installing GPU dependencies..."
    apt-get install -y --no-install-recommends \
        libudev-dev \
        libgudev-1.0-dev \
        libglvnd-dev \
        libgl1-mesa-dev \
        libgles2-mesa-dev \
        libegl1-mesa-dev \
        libglx-dev

    # Install nvidia driver
    apt-get install -y software-properties-common
    add-apt-repository ppa:graphics-drivers/ppa -y
    apt-get update
    apt-get install -y nvidia-driver-550-server

    # CUDA is not strictly required but may be useful in the future
    # wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2204/x86_64/cuda-keyring_1.1-1_all.deb
    # dpkg -i cuda-keyring_1.1-1_all.deb
    # apt-get update
    # apt-get install -y cuda-driver-dev-12-4 
else
    echo "Skipping GPU dependencies (ENABLE_GPU != 'true')"
fi

# Clean up
apt-get clean
rm -rf /var/lib/apt/lists/*
