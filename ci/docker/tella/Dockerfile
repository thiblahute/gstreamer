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
  nasm \
  python3-dev \
  python3-pip \
  shared-mime-info \
  wget \
  xdg-dbus-proxy \
  yasm \
  zlib1g-dev && \
  apt-get clean && \
  rm -rf /var/lib/apt/lists/* && \
  pip3 install meson==0.63.0 --no-binary :all: && \
  pip3 install ninja tomli

# Install rust toolchain
RUN curl https://sh.rustup.rs -sSf | sh -s -- --default-toolchain nightly -y
ENV PATH="/root/.cargo/bin:${PATH}"
RUN cargo install cargo-c --git https://github.com/evaporei/cargo-c.git

FROM deps as cairo

# Build cairo
RUN curl -L --silent -o cairo-1.17.8.tar.xz https://cairographics.org/snapshots/cairo-1.17.8.tar.xz && \
  tar -xf cairo-1.17.8.tar.xz && \
  cd cairo-1.17.8 && \
  meson setup builddir --prefix=/usr --buildtype=release --default-library=shared && \
  ninja -C builddir && \
  mkdir /opt/cairo && \
  DESTDIR=/opt/cairo ninja -C builddir install && \
  cd .. && \
  rm -rf cairo-1.17.8 && \
  rm cairo-1.17.8.tar.xz

FROM deps as pango

COPY --from=cairo /opt/cairo /
RUN curl -L --silent -o pango-1.50.6.tar.xz https://download.gnome.org/sources/pango/1.50/pango-1.50.6.tar.xz && \
  tar -xf pango-1.50.6.tar.xz && \
  cd pango-1.50.6 && \
  meson setup builddir --prefix=/usr --buildtype=release --default-library=shared && \
  ninja -C builddir && \
  mkdir /opt/pango && \
  DESTDIR=/opt/pango ninja -C builddir install && \
  cd .. && \
  rm -rf pango-1.50.6 && \
  rm pango-1.50.6.tar.xz

FROM deps as gdk-pixbuf
RUN curl -L --silent -o gdk-pixbuf-2.42.8.tar.xz https://download.gnome.org/sources/gdk-pixbuf/2.42/gdk-pixbuf-2.42.8.tar.xz && \
  tar -xf gdk-pixbuf-2.42.8.tar.xz && \
  cd gdk-pixbuf-2.42.8 && \
  meson setup builddir --prefix=/usr --buildtype=release --default-library=shared && \
  ninja -C builddir && \
  mkdir /opt/gdk-pixbuf && \
  DESTDIR=/opt/gdk-pixbuf ninja -C builddir install && \
  cd .. && \
  rm -rf gdk-pixbuf-2.42.8 && \
  rm gdk-pixbuf-2.42.8.tar.xz

FROM deps as librsvg
COPY --from=cairo /opt/cairo /
COPY --from=pango /opt/pango /
COPY --from=gdk-pixbuf /opt/gdk-pixbuf /

# Build librsvg
RUN curl -L --silent -o librsvg-2.52.5.tar.xz https://download.gnome.org/sources/librsvg/2.52/librsvg-2.52.5.tar.xz && \
  tar -xf librsvg-2.52.5.tar.xz && \
  cd librsvg-2.52.5 && \
  ./configure CFLAGS=-O2 --prefix=/usr --enable-shared --disable-static --disable-introspection --disable-tools --disable-maintainer-mode --disable-dependency-tracking --disable-gtk-doc && \
  make DESTDIR=/opt/librsvg install && \
  cd .. && \
  rm -rf librsvg-2.52.5 && \
  rm librsvg-2.52.5.tar.xz

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
COPY --from=cairo /opt/cairo /
COPY --from=pango /opt/pango /
COPY --from=librsvg /opt/librsvg /
COPY --from=gdk-pixbuf /opt/gdk-pixbuf /

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
  -Dintrospection=enabled \
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
  -Dgst-plugins-good:soup=enabled \
  -Dgst-plugins-good:spectrum=enabled \
  -Dgst-plugins-good:udp=enabled \
  -Dgst-plugins-good:v4l2=disabled \
  -Dgst-plugins-good:videobox=enabled \
  -Dgst-plugins-good:videocrop=enabled \
  -Dgst-plugins-good:videofilter=enabled \
  -Dgst-plugins-good:videomixer=enabled \
  -Dgst-plugins-good:ximagesrc=disabled \
  -Dbad=enabled \
  -Dgst-plugins-bad:bz2=disabled \
  -Dgst-plugins-bad:closedcaption=disabled \
  -Dgst-plugins-bad:codec2json=disabled \
  -Dgst-plugins-bad:curl=disabled \
  -Dgst-plugins-bad:drm=disabled \
  -Dgst-plugins-bad:kms=disabled \
  -Dgst-plugins-bad:librfb=disabled \
  -Dgst-plugins-bad:mpegtsdemux=enabled \
  -Dgst-plugins-bad:mpegtsmux=enabled \
  -Dgst-plugins-bad:msdk=disabled \
  -Dgst-plugins-bad:msdk=disabled \
  -Dgst-plugins-bad:nvcodec=disabled \
  -Dgst-plugins-bad:openh264=disabled \
  -Dgst-plugins-bad:qroverlay=disabled \
  -Dgst-plugins-bad:rsvg=enabled \
  -Dgst-plugins-bad:ttml=disabled \
  -Dgst-plugins-bad:videoparsers=enabled \
  -Dgst-plugins-bad:wayland=disabled \
  -Dgst-plugins-bad:webrtc=disabled \
  -Dugly=enabled \
  -Dgst-plugins-ugly:x264=enabled \
  -Dgst-plugins-bad:x265=enabled \
  -Drs=disabled && \
  ninja -C builddir

RUN mkdir /opt/gstreamer && DESTDIR=/opt/gstreamer ninja -C builddir install && rm -rf builddir

# Make ffmpeg output available under /opt/gstreamer for easy copying
COPY --from=ffmpeg /opt/ffmpeg /opt/gstreamer/usr
COPY --from=cairo /opt/cairo /opt/gstreamer
COPY --from=pango /opt/pango /opt/gstreamer
COPY --from=gdk-pixbuf /opt/gdk-pixbuf /opt/gstreamer
COPY --from=librsvg /opt/librsvg /opt/gstreamer
