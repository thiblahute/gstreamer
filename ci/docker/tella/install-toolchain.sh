#!/bin/bash

set -eo pipefail


curl https://sh.rustup.rs -sSf | sh -s -- -y
cargo install --locked cargo-c --version 0.10.11+cargo-0.86.0

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
