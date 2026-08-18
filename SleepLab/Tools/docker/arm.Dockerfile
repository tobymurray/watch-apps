# The image `docker-build.sh app|probe` builds .uapp files in.
#
# Layered on the toolchain Kira publishes binaries with, so a .uapp built here
# is the same artifact the catalogue would carry -- pinned by digest for the
# same reason Kira pins it. All this adds is what `app_packer.py` needs, which
# the base image does not carry.
FROM ghcr.io/tobymurray/kira-toolchain@sha256:4d75a70b33b4c4fb8799bed5af2ebb03c9b6eb161ffed3049e316ad996461127
RUN pip3 install --break-system-packages pyelftools Pillow
