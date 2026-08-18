# The image `docker-build.sh sim|sim-run` builds and runs the TouchGFX Linux
# simulator in.
#
# Three requirements that together rule out the host-test image:
#   - SDL2, for the simulator itself;
#   - Ruby, because TouchGFX's text and image generators are Ruby scripts;
#   - amd64, because some of those generators are amd64-only binaries. On
#     Apple silicon this runs emulated, which is slow and correct.
FROM una-armgcc-ruby:latest
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      g++ make libsdl2-dev libsdl2-image-dev pkg-config imagemagick \
 && rm -rf /var/lib/apt/lists/*
