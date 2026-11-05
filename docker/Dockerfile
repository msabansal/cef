# syntax=docker/dockerfile:1
ARG BASE_IMAGE=ubuntu:26.10
FROM ${BASE_IMAGE} AS builder

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
        procps util-linux passwd pkg-config libatomic1 \
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
