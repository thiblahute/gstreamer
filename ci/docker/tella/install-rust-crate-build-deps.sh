#!/bin/bash

set -eo pipefail

# Set noninteractive frontend
export DEBIAN_FRONTEND=noninteractive

# Update package lists
apt-get update

# Install only the minimal dev packages needed for Rust GStreamer bindings
# The bindings need pkg-config tool and system dev packages for glib/gobject
# that GStreamer depends on. We don't need any GStreamer build tools.
apt-get install -y --no-install-recommends \
    pkg-config \
    libglib2.0-dev \
    libgirepository1.0-dev \
    libssl-dev \
    libunwind-dev \
    libxml2-dev \
    libfontconfig1-dev \
    libclang-dev \
    git \
    curl \
    protobuf-compiler \
    libprotobuf-dev

# Clean up
apt-get clean
rm -rf /var/lib/apt/lists/*
