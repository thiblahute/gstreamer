FROM ubuntu:22.04 as deps

# Install dependencies
RUN export DEBIAN_FRONTEND=noninteractive && \
  apt-get update && \
  apt-get dist-upgrade -y && \
  apt-get install -y --reinstall --purge fontconfig-config --option=Dpkg::Options::=--force-confdef && \
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
  librsvg2-dev \
  nasm \
  python3-dev \
  python3-pip \
  shared-mime-info \
  wget \
  xdg-dbus-proxy \
  yasm \
  zlib1g-dev \
  gdb \
  fontconfig \
  alien \
  fonts-noto-cjk \
  fonts-noto \
  && \
  apt-get clean && \
  rm -rf /var/lib/apt/lists/* && \
  pip3 install meson==1.4.0 --no-binary :all: && \
  pip3 install ninja tomli

# Install rust toolchain
RUN curl https://sh.rustup.rs -sSf | sh -s -- -y
ENV PATH="/root/.cargo/bin:${PATH}"
RUN cargo install cargo-c

# Build FFmpeg
FROM deps as ffmpeg
COPY ci/docker/tella/build-ffmpeg /
RUN ["/build-ffmpeg", "--build"]

# FROM ffmpeg as ffmpeg-test
# COPY --from=ffmpeg /opt/ffmpeg /usr

# And then: docker build . -f ci/docker/tella/Dockerfile --target ffmpeg-test --platform linux/arm64/v8 -t 486029010931.dkr.ecr.us-east-2.amazonaws.com/gstreamer-base-image:ffmpeg
# docker run --rm 486029010931.dkr.ecr.us-east-2.amazonaws.com/gstreamer-base-image:ffmpeg ffmpeg

FROM deps as gstreamer

# Copy deps to /usr so GStreamer build can pick it up
COPY --from=ffmpeg /opt/ffmpeg /usr

COPY ci/fuzzing  /gstreamer/ci/fuzzing
COPY data /gstreamer/data
COPY girs /gstreamer/girs
COPY scripts /gstreamer/scripts
COPY subprojects /gstreamer/subprojects
COPY tests /gstreamer/tests
COPY ci /gstreamer/ci
COPY gst-env.py meson.build meson_options.txt /gstreamer/

WORKDIR /gstreamer

# Configure, build and install GStreamer
RUN meson builddir \
  --prefix /usr \
  --native-file ci/docker/tella/native.ini \
  -Dpackage-origin="Tella $(git rev-parse --short HEAD)"

RUN ninja -C builddir install && rm -rf builddir

# Generate the GStreamer plugins registry file and make it read-only.
ENV GST_REGISTRY=/etc/gstreamer/registry-1.0.bin
RUN mkdir -p /etc/gstreamer
RUN gst-inspect-1.0
RUN gst-inspect-1.0 --version
RUN chmod a+r /etc/gstreamer/registry-1.0.bin
ENV GST_REGISTRY_UPDATE=no
ENV GST_REGISTRY_FORK=no
