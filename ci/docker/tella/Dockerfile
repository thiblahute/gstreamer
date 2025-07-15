###############################################################################
# build-deps                                                                  #
#                                                                             #
# This stage contains the dependencies for building GStreamer minus the       #
# toolchain.                                                                  #
###############################################################################
FROM ubuntu:22.04 AS build-deps
COPY ci/docker/tella/install-build-deps.sh /install-build-deps.sh
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
RUN git clone --depth 1 --branch 1.5.0 https://code.videolan.org/videolan/dav1d.git /opt/dav1d && \
  cd /opt/dav1d && \
  meson build --buildtype release && \
  ninja -C build install && \
  ldconfig
COPY ci/docker/tella/build-ffmpeg.sh /build-ffmpeg.sh
RUN /build-ffmpeg.sh --build

###############################################################################
# build-deps-toolchain-gstreamer                                              #
#                                                                             #
# This stage contains the dev dependencies, toolchain, and compiled GStreamer #
# in /compiled-binaries.                                                      #
###############################################################################
FROM build-deps-toolchain AS build-deps-toolchain-gstreamer

# Copy FFmpeg to /usr so GStreamer build can pick it up
COPY --from=build-deps-ffmpeg /opt/ffmpeg /usr

COPY ci/fuzzing  /gstreamer/ci/fuzzing
COPY data /gstreamer/data
COPY girs /gstreamer/girs
COPY scripts /gstreamer/scripts
COPY subprojects /gstreamer/subprojects
COPY tests /gstreamer/tests
COPY ci /gstreamer/ci
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
# build-deps-gstreamer                                                        #
#                                                                             #
# This stage contains the dev dependencies and compiled GStreamer in          #
# /compiled-binaries but NO toolchain.                                        #
###############################################################################
FROM build-deps AS build-deps-gstreamer
COPY --from=build-deps-ffmpeg /opt/ffmpeg /usr
COPY --from=build-deps-toolchain-gstreamer /compiled-binaries /

###############################################################################
# release-deps                                                                #
#                                                                             #
# This stage contains ONLY the prod dependencies.                             #
###############################################################################
FROM ubuntu:22.04 AS release-deps
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
# Preventing registry updates becomes a problem when using plugins with
# runtime dependencies which are not available at build time. For example,
# we install basic NVIDIA drivers at build time so that we have the required
# headers for nvcodec to compile, but most of the NVIDIA runtime libraries are
# injected at runtime.
# When we want to start using nvcodec, we will need to find a way to
# conditionally enable registry updates.
ENV GST_REGISTRY_UPDATE=no
ENV GST_REGISTRY_FORK=no

ENTRYPOINT []
