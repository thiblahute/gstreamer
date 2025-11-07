#!/bin/bash

set -eo pipefail

# Check if RUST_ONLY mode is enabled (for rust-build-deps stage)
RUST_ONLY=${RUST_ONLY:-false}

curl https://sh.rustup.rs -sSf | sh -s -- -y --profile minimal
export PATH="/root/.cargo/bin:/usr/local/cargo/bin:${PATH}"
cargo install --locked cargo-c --version 0.10.11+cargo-0.86.0
cargo install --locked cargo-chef --version 0.1.72

# Install Python build tools (only in full mode)
if [ "${RUST_ONLY}" != "true" ]; then
    apt-get update

    apt-get install -y --no-install-recommends \
        python3-dev \
        python3-pip

    # Install Python packages
    pip3 install meson==1.4.0 --no-binary :all:
    pip3 install ninja tomli

    # Clean up
    apt-get clean
    rm -rf /var/lib/apt/lists/*
fi
