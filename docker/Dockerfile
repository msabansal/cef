# syntax=docker/dockerfile:1
ARG BASE_IMAGE=ubuntu:26.10
# Copy a runnable target userspace; no ARM64 build step executes under emulation.
ARG ARM64_UNIT_RUNTIME_IMAGE=buildpack-deps:bookworm@sha256:f719cedddb6ca673716cb4564b9298bbaa7fea8b4ae8a78e4ead2b2299ed6c62
ARG UNIT_PACKAGE_BUILDER_IMAGE=buildpack-deps:bookworm@sha256:cba7fa3c133e3110d747518182a10a8f969691e6788abea5f454ce7f83de1542
FROM --platform=linux/arm64 ${ARM64_UNIT_RUNTIME_IMAGE} AS arm64-unit-runtime

FROM --platform=linux/amd64 ${UNIT_PACKAGE_BUILDER_IMAGE} AS arm64-unit-packages
RUN test "$(dpkg --print-architecture)" = amd64 \
    && mkdir -p /packages/partial /runtime \
    && dpkg --add-architecture arm64 \
    && apt-get update \
    && apt-get install --download-only -y --no-install-recommends \
        -o Dir::Cache::archives=/packages \
        libasound2:arm64 libatk-bridge2.0-0:arm64 libatk1.0-0:arm64 \
        libcairo2:arm64 libcups2:arm64 libdbus-1-3:arm64 libexpat1:arm64 \
        libgbm1:arm64 libglib2.0-0:arm64 libgtk-3-0:arm64 \
        libnspr4:arm64 libnss3:arm64 libpango-1.0-0:arm64 \
        libpangocairo-1.0-0:arm64 libudev1:arm64 \
        libx11-6:arm64 libx11-xcb1:arm64 libxcb1:arm64 \
        libxcomposite1:arm64 libxdamage1:arm64 libxext6:arm64 \
        libxfixes3:arm64 libxkbcommon0:arm64 libxrandr2:arm64 \
    && for package in /packages/*.deb; do \
        architecture=$(dpkg-deb --field "$package" Architecture); \
        case "$architecture" in \
            arm64|all) dpkg-deb --extract "$package" /runtime ;; \
        esac; \
    done

FROM ${BASE_IMAGE} AS builder
COPY --from=arm64-unit-runtime /lib/aarch64-linux-gnu/ /opt/reach-arm64-unit-runtime/lib/aarch64-linux-gnu/
COPY --from=arm64-unit-runtime /lib/ld-linux-aarch64.so.1 /opt/reach-arm64-unit-runtime/lib/
COPY --from=arm64-unit-packages /runtime/ /opt/reach-arm64-unit-runtime/

# Chromium supplies the target sysroot and Clang. These packages also support
# executing host tools that are built against that sysroot. Standalone Skia's
# GN host toolchain additionally needs the system C/C++ compiler and headers.
RUN --mount=type=cache,id=cef-ubuntu-apt,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,id=cef-ubuntu-apt-lists,target=/var/lib/apt/lists,sharing=locked \
    rm -f /etc/apt/apt.conf.d/docker-clean \
    && apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        bash ca-certificates g++ git python3 rsync \
        tar gzip bzip2 xz-utils unzip zip curl file findutils diffutils patch \
        procps util-linux passwd pkg-config libatomic1 qemu-user \
        libglib2.0-0t64 libnss3 libnspr4 libatk1.0-0t64 libatk-bridge2.0-0t64 \
        libcups2t64 libdbus-1-3 libexpat1 \
        libx11-6 libx11-xcb1 libxcomposite1 libxdamage1 libxext6 libxfixes3 \
        libxrandr2 libxcb1 libxkbcommon0 libgbm1 libasound2t64 \
        libpango-1.0-0 libpangocairo-1.0-0 libcairo2

ARG BUILDER_UID=1000
ARG BUILDER_GID=1000
RUN if getent passwd ubuntu >/dev/null; then userdel ubuntu; fi \
    && if getent group ubuntu >/dev/null; then groupdel ubuntu; fi \
    && groupadd --gid "${BUILDER_GID}" builder \
    && useradd --uid "${BUILDER_UID}" --gid "${BUILDER_GID}" \
        --home-dir /work/home --no-create-home builder \
    && mkdir -p /work/home/.cache /artifacts \
    && chown -R builder:builder /work /artifacts

ENV HOME=/work/home \
    XDG_CACHE_HOME=/work/home/.cache \
    PATH=/work/depot_tools:${PATH} \
    DEPOT_TOOLS_UPDATE=0 \
    PYTHONUNBUFFERED=1 \
    PYTHONDONTWRITEBYTECODE=1 \
    LANG=C.UTF-8

COPY build.sh reset_patched_files.py /usr/local/lib/cef/
USER builder
WORKDIR /work
ENTRYPOINT ["bash", "/usr/local/lib/cef/build.sh"]
CMD ["all"]

# builder-official mounts its own volume at /work/chromium/src/out. Docker
# initializes an empty named volume from the image directory at the mount
# target, so the directory must exist here and be owned by the builder user.
FROM builder AS official
USER root
RUN mkdir -p /work/chromium/src/out \
    && chown builder:builder /work/chromium /work/chromium/src \
        /work/chromium/src/out
USER builder

# Keep the release builder as the default build target.
FROM builder
