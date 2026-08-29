# The container every app's binary is built in here, and the reason CI needs no
# cross-repository package grant to work: a workflow in this repository can
# always pull a package this same repository publishes, with no extra setting
# anywhere. Depending on tobymurray/kira's own kira-toolchain image instead once
# meant every build here was one GHCR "Manage Actions access" grant away from
# working, and that grant was never made -- see the commit that added this file.
#
# Copied from tobymurray/kira's toolchain/Dockerfile rather than written fresh,
# so a binary built here is built by the same recipe as the one Kira's own
# catalogue uses -- same base image, same packages, same pinned Rust. Owning a
# copy also means this repository's builds no longer depend on a third
# repository's package outliving this one's pin to a digest of it.

# The ARM toolchain, pinned. The same image Kira's catalogue has always been
# built with.
FROM xanderhendriks/stm32cubeide:16.0@sha256:7e07c508e3944def22eaabe822eaf902a2ba4fbb38a3ce24d6ff874f9f04c447

# Ubuntu's security mirror has hung builds before; the main archive carries the
# same packages.
RUN find /etc/apt -type f \( -name '*.list' -o -name '*.sources' \) \
      -exec sed -i 's#security\.ubuntu\.com/ubuntu#archive.ubuntu.com/ubuntu#g' {} + || true \
 && apt-get update \
 && apt-get install -y --no-install-recommends \
      bash git curl ca-certificates jq \
      cmake make \
      python3-full python3-pip python-is-python3 \
      # The SDK's app packer imports both, and cmake runs it after every app
      # links, so an image without them can pack nothing.
      python3-pyelftools python3-pil \
 && rm -rf /var/lib/apt/lists/*

# The image ships these unexecutable, and every job had to fix that itself.
RUN for tool in arm-none-eabi-gcc arm-none-eabi-g++ arm-none-eabi-objcopy arm-none-eabi-size; do \
      path="$(find /opt/st/stm32cubeide_* -type f -name "$tool" | head -n 1)"; \
      [ -n "$path" ] && chmod +x "$path" || true; \
    done

# Put the ARM tools on PATH once, rather than in each job. Deliberately not the
# bundled `make`: it is a wrapper that sources an environment file this image
# does not have, and on PATH it would shadow the real one from apt.
ENV PATH="/opt/st/toolchain/bin:${PATH}"
RUN mkdir -p /opt/st/toolchain/bin \
 && for tool in arm-none-eabi-gcc arm-none-eabi-g++ arm-none-eabi-objcopy arm-none-eabi-size arm-none-eabi-objdump; do \
      path="$(find /opt/st/stm32cubeide_* -type f -name "$tool" | head -n 1)"; \
      [ -n "$path" ] && ln -sf "$path" "/opt/st/toolchain/bin/$tool" || true; \
    done

# Rust, for an app whose sources are not all C -- none wired into this
# repository's CI yet, but kept so a future one needs no image change. Pinned
# exactly: "stable" would make the image's contents depend on when it was
# built, and every binary built here would inherit that.
ENV RUSTUP_HOME=/opt/rust/rustup \
    CARGO_HOME=/opt/rust/cargo \
    PATH=/opt/rust/cargo/bin:/opt/st/toolchain/bin:${PATH}
ARG RUST_VERSION=1.97.0
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs \
      | sh -s -- -y --no-modify-path --profile minimal \
        --default-toolchain "${RUST_VERSION}" \
        --target thumbv8m.main-none-eabihf \
        --target thumbv7em-none-eabihf \
 && chmod -R a+rX /opt/rust \
 && rustc --version && cargo --version

# Cargo does reach the network, because a crate's dependencies are not in the
# image. That is tolerable rather than ideal: Cargo.lock pins each dependency to
# an exact version and checksum, so the fetch is deterministic as long as the app
# commits its lock file. An app that does not is not reproducible here, whatever
# this image does.

LABEL org.opencontainers.image.source=https://github.com/tobymurray/watch-apps \
      org.opencontainers.image.description="ARM toolchain plus Rust, for building UNA Watch apps in this repository reproducibly" \
      org.opencontainers.image.licenses=MIT
