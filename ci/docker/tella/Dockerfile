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
COPY gst-env.py meson.build meson_options.txt /gstreamer/

WORKDIR /gstreamer

# Configure, build and install GStreamer
RUN meson builddir \
  --prefix /usr \
  -Db_lto=true \
  -Dbuildtype=debugoptimized \
  -Dintrospection=disabled \
  -Dgstreamer:tools=enabled \
  -Dpython=disabled \
  -Dorc=enabled \
  -Ddevtools=disabled \
  -Drtsp_server=disabled \
  -Dges=enabled \
  -Dgpl=enabled \
  -Dlibav=enabled \
  -Dbase=enabled \
  -Dgstreamer:check=enabled \
  -Dgst-examples=disabled \
  -Dgst-plugins-base:examples=disabled \
  -Dgst-plugins-base:tests=disabled \
  -Dgst-plugins-base:adder=enabled \
  -Dgst-plugins-base:app=enabled \
  -Dgst-plugins-base:audioconvert=enabled \
  -Dgst-plugins-base:audiomixer=enabled \
  -Dgst-plugins-base:audiorate=enabled \
  -Dgst-plugins-base:audioresample=enabled \
  -Dgst-plugins-base:audiotestsrc=enabled \
  -Dgst-plugins-base:compositor=enabled \
  -Dgst-plugins-base:encoding=enabled \
  -Dgst-plugins-base:gl=disabled \
  -Dgst-plugins-base:pango=disabled \
  -Dgst-plugins-base:overlaycomposition=enabled \
  -Dgst-plugins-base:pbtypes=enabled \
  -Dgst-plugins-base:playback=enabled \
  -Dgst-plugins-base:rawparse=enabled \
  -Dgst-plugins-base:subparse=enabled \
  -Dgst-plugins-base:tcp=enabled \
  -Dgst-plugins-base:typefind=enabled \
  -Dgst-plugins-base:videoconvertscale=enabled \
  -Dgst-plugins-base:videorate=enabled \
  -Dgst-plugins-base:videotestsrc=enabled \
  -Dgst-plugins-base:volume=enabled \
  -Dgst-plugins-base:x11=disabled \
  -Dgood=enabled \
  -Dgst-plugins-good:adaptivedemux2=enabled \
  -Dgst-plugins-good:alpha=enabled \
  -Dgst-plugins-good:asm=enabled \
  -Dgst-plugins-good:audioparsers=enabled \
  -Dgst-plugins-good:autodetect=enabled \
  -Dgst-plugins-good:cairo=disabled \
  -Dgst-plugins-good:cutter=enabled \
  -Dgst-plugins-good:debugutils=enabled \
  -Dgst-plugins-good:gtk3=disabled \
  -Dgst-plugins-good:imagefreeze=enabled \
  -Dgst-plugins-good:interleave=enabled \
  -Dgst-plugins-good:isomp4=enabled \
  -Dgst-plugins-good:jack=disabled \
  -Dgst-plugins-good:law=enabled \
  -Dgst-plugins-good:level=enabled \
  -Dgst-plugins-good:matroska=enabled \
  -Dgst-plugins-good:multifile=enabled \
  -Dgst-plugins-good:multipart=enabled \
  -Dgst-plugins-good:png=enabled \
  -Dgst-plugins-good:replaygain=enabled \
  -Dgst-plugins-good:smpte=enabled \
  -Dgst-plugins-good:spectrum=enabled \
  -Dgst-plugins-good:udp=enabled \
  -Dgst-plugins-good:v4l2=disabled \
  -Dgst-plugins-good:videobox=enabled \
  -Dgst-plugins-good:videocrop=enabled \
  -Dgst-plugins-good:videofilter=enabled \
  -Dgst-plugins-good:videomixer=enabled \
  -Dgst-plugins-good:ximagesrc=disabled \
  -Dgst-plugins-good:soup=enabled \
  -Dgst-plugins-good:wavenc=enabled \
  -Dbad=enabled \
  -Dgst-plugins-bad:applemedia=disabled \
  -Dgst-plugins-bad:bz2=disabled \
  -Dgst-plugins-bad:closedcaption=disabled \
  -Dgst-plugins-bad:codec2json=disabled \
  -Dgst-plugins-bad:curl=disabled \
  -Dgst-plugins-bad:drm=disabled \
  -Dgst-plugins-bad:faac=disabled \
  -Dgst-plugins-bad:fdkaac=enabled \
  -Dgst-plugins-bad:kms=disabled \
  -Dgst-plugins-bad:librfb=disabled \
  -Dgst-plugins-bad:mpegtsdemux=enabled \
  -Dgst-plugins-bad:mpegtsmux=enabled \
  -Dgst-plugins-bad:msdk=disabled \
  -Dgst-plugins-bad:nvcodec=disabled \
  -Dgst-plugins-bad:openh264=disabled \
  -Dgst-plugins-bad:qroverlay=disabled \
  -Dgst-plugins-bad:rsvg=enabled \
  -Dgst-plugins-bad:ttml=disabled \
  -Dgst-plugins-bad:videoparsers=enabled \
  -Dgst-plugins-bad:wayland=disabled \
  -Dgst-plugins-bad:webrtc=disabled \
  -Dgst-plugins-bad:aja=disabled \
  -Dugly=enabled \
  -Dgst-plugins-ugly:x264=enabled \
  -Dgst-plugins-bad:x265=enabled \
  -Drs=disabled && \
  ninja -C builddir

RUN ninja -C builddir install && rm -rf builddir

# Generate the GStreamer plugins registry file and make it read-only.
ENV GST_REGISTRY=/etc/gstreamer/registry-1.0.bin
RUN mkdir -p /etc/gstreamer
RUN gst-inspect-1.0
RUN chmod a+r /etc/gstreamer/registry-1.0.bin
ENV GST_REGISTRY_UPDATE=no
ENV GST_REGISTRY_FORK=no
