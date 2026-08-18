# The image `docker-build.sh tests` runs host tests in.
#
# Any image with cmake and a C++17 g++ would do; python3 is the one addition
# that matters, because without it the probe-report round-trip test -- the one
# that parses the real writer's output with the real host script -- is skipped
# rather than run.
#
# The base is whatever this machine already had; substitute any Debian-family
# image with build-essential and cmake.
FROM una-hosttests:latest
RUN apt-get update \
 && apt-get install -y --no-install-recommends python3 \
 && rm -rf /var/lib/apt/lists/*
