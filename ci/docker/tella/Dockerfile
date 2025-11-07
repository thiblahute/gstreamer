###############################################################################
# build-deps                                                                  #
#                                                                             #
# This stage contains the dependencies for building GStreamer minus the       #
# toolchain.                                                                  #
###############################################################################
FROM ubuntu:22.04 AS build-deps
COPY ci/docker/tella/install-build-deps.sh /install-build-deps.sh
ENV ENABLE_GPU=true
RUN /install-build-deps.sh


###############################################################################
# build-deps-toolchain                                                        #
#                                                                             #
# This stage contains the dependencies AND toolchain for building GStreamer.  #
###############################################################################
FROM build-deps AS build-deps-toolchain
COPY ci/docker/tella/install-toolchain.sh /install-toolchain.sh
ENV PATH="/root/.cargo/bin:${PATH}"
RUN /install-toolchain.sh

###############################################################################
# build-ffmpeg                                                                #
#                                                                             #
# This stage contains the dependencies for building GStreamer plus a          #
# built-from-source FFmpeg.                                                   #
###############################################################################
FROM build-deps-toolchain as build-deps-ffmpeg
# Install dav1d for AV1 decoding
# Download tarball instead of git clone to avoid TLS/HTTP2 issues with code.videolan.org
RUN curl -L --retry 3 --retry-delay 5 -o /tmp/dav1d-1.5.0.tar.xz https://download.videolan.org/pub/videolan/dav1d/1.5.0/dav1d-1.5.0.tar.xz && \
  tar -xf /tmp/dav1d-1.5.0.tar.xz -C /opt && \
  mv /opt/dav1d-1.5.0 /opt/dav1d && \
  cd /opt/dav1d && \
  meson build --buildtype release && \
  ninja -C build install && \
  ldconfig && \
  rm /tmp/dav1d-1.5.0.tar.xz
COPY ci/docker/tella/build-ffmpeg.sh /build-ffmpeg.sh
RUN /build-ffmpeg.sh --build

###############################################################################
# build-deps-toolchain-gstreamer                                              #
#                                                                             #
# This stage contains the dev dependencies, toolchain, and compiled GStreamer #
# in /compiled-binaries.                                                      #
###############################################################################
FROM build-deps-toolchain AS build-deps-toolchain-gstreamer
ENV SKIA_USE_PKG_CONFIG=1

# Copy FFmpeg to /usr so GStreamer build can pick it up
COPY --from=build-deps-ffmpeg /opt/ffmpeg /usr

COPY ci/fuzzing  /gstreamer/ci/fuzzing
COPY ci/meson /gstreamer/ci/meson
COPY ci/scripts /gstreamer/ci/scripts
COPY ci/docker/tella/native.ini /gstreamer/ci/docker/tella/native.ini
COPY data /gstreamer/data
COPY girs /gstreamer/girs
COPY scripts /gstreamer/scripts
COPY subprojects /gstreamer/subprojects
COPY tests /gstreamer/tests
COPY gst-env.py meson.build meson_options.txt /gstreamer/

# Configure, build and install GStreamer
WORKDIR /gstreamer
RUN meson builddir \
  --prefix /usr \
  --native-file ci/docker/tella/native.ini \
  -Dpackage-origin="Tella $(git rev-parse --short HEAD)"
RUN DESTDIR=/compiled-binaries ninja -C builddir install && rm -rf builddir

ENTRYPOINT []

###############################################################################
# release-deps                                                                #
#                                                                             #
# This stage contains ONLY the prod dependencies.                             #
###############################################################################
FROM ubuntu:22.04 AS release-deps
ENV SKIA_USE_PKG_CONFIG=1
ENV ENABLE_GPU=true
COPY --from=build-deps-ffmpeg /opt/ffmpeg /usr
# Copy dav1d shared libraries necessary for AV1 decoding
RUN --mount=from=build-deps-ffmpeg,source=/usr/local/lib,target=/tmp/src_lib \
    find /tmp/src_lib -name "libdav1d.so*" -type f -exec cp -P {} /usr/local/lib/ \;
COPY ci/docker/tella/install-release-deps.sh /install-release-deps.sh
RUN /install-release-deps.sh

###############################################################################
# release-deps-gstreamer                                                      #
#                                                                             #
# This stage contains the prod dependencies and compiled GStreamer in /usr,   #
# ready to be used in a production environment.                               #
###############################################################################
FROM release-deps AS release-deps-gstreamer
COPY --from=build-deps-toolchain-gstreamer /compiled-binaries /

# Generate the GStreamer plugins registry file and make it read-only.
ENV GST_REGISTRY=/etc/gstreamer/registry-1.0.bin
RUN mkdir -p /etc/gstreamer
RUN gst-inspect-1.0
RUN gst-inspect-1.0 --version
RUN chmod a+r /etc/gstreamer/registry-1.0.bin

COPY ci/docker/tella/entrypoint.sh /entrypoint.sh
ENTRYPOINT ["/entrypoint.sh"]

###############################################################################
# rust-build-deps                                                              #
#                                                                             #
# This stage contains the minimal dependencies needed to build Rust crates     #
# that use GStreamer (temporal, video_renderer). It includes the compiled     #
# GStreamer binaries (with headers and pkg-config files), the system dev       #
# packages needed for Rust bindings compilation, and the Rust toolchain.      #
###############################################################################
FROM release-deps-gstreamer AS rust-build-deps
ENV SKIA_USE_PKG_CONFIG=1

# Set PKG_CONFIG_PATH so pkg-config can find GStreamer .pc files
# GStreamer is installed to /usr, so pkg-config files are in /usr/lib/pkgconfig
# and potentially in architecture-specific paths
ENV PKG_CONFIG_PATH="/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/lib/pkgconfig:/usr/share/pkgconfig"

# Install only the minimal dev packages needed for Rust GStreamer bindings
COPY ci/docker/tella/install-rust-crate-build-deps.sh /install-rust-crate-build-deps.sh
RUN /install-rust-crate-build-deps.sh

# Install Rust toolchain (rust-only mode: minimal profile, cargo-c, cargo-chef)
COPY ci/docker/tella/install-toolchain.sh /install-toolchain.sh
RUN RUST_ONLY=true /install-toolchain.sh && \
    rm -rf /root/.cargo/registry/src && \
    rm -rf /root/.cargo/registry/cache
ENV PATH="/root/.cargo/bin:/usr/local/cargo/bin:${PATH}"
