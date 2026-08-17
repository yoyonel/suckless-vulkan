FROM debian:trixie-slim

ARG JUST_VERSION=1.46.0
ARG HADOLINT_VERSION=2.12.0
ARG ACTIONLINT_VERSION=1.7.4

ENV DEBIAN_FRONTEND=noninteractive

# hadolint ignore=DL3008
RUN apt-get update && apt-get install -y --no-install-recommends \
    bash \
    ca-certificates \
    build-essential \
    clang \
    clang-tidy \
    cmake \
    curl \
    git \
    libclang-rt-dev \
    llvm \
    ninja-build \
    pkg-config \
    libglfw3-dev \
    libglm-dev \
    libvulkan-dev \
    libdbus-1-dev \
    libssl-dev \
    vulkan-tools \
    mesa-vulkan-drivers \
    glslang-tools \
    xvfb \
    xauth \
    xdotool \
    shellcheck \
    ccache \
    python3 \
    python3-pip \
    gdb \
    && rm -rf /var/lib/apt/lists/*

ENV XDG_RUNTIME_DIR=/tmp
ENV XDG_SESSION_TYPE=x11

# Use pipefail so pipe errors propagate correctly in subsequent RUN steps
SHELL ["/bin/bash", "-o", "pipefail", "-c"]

# just - not packaged in Debian bookworm
RUN curl -fsSL \
    "https://github.com/casey/just/releases/download/${JUST_VERSION}/just-${JUST_VERSION}-x86_64-unknown-linux-musl.tar.gz" \
    | tar -xz -C /usr/local/bin just

# hadolint - Dockerfile linter and security checker
RUN curl -fsSL \
    "https://github.com/hadolint/hadolint/releases/download/v${HADOLINT_VERSION}/hadolint-Linux-x86_64" \
    -o /usr/local/bin/hadolint \
    && chmod +x /usr/local/bin/hadolint

# actionlint - GitHub Actions workflow static checker
RUN curl -fsSL \
    "https://github.com/rhysd/actionlint/releases/download/v${ACTIONLINT_VERSION}/actionlint_${ACTIONLINT_VERSION}_linux_amd64.tar.gz" \
    | tar -xz -C /usr/local/bin actionlint

# uv provides uvx used by justfile recipes; lint tools installed directly
# so container runs do not depend on live package index access.
RUN pip3 install --break-system-packages --no-cache-dir \
    uv==0.10.11 \
    cmakelang==0.6.13 \
    yamllint==1.38.0 \
    pymarkdownlnt==0.9.36 \
    ruff==0.2.1

# Pre-compile Tracy CLI tools (tracy-capture, tracy-csvexport)
ARG TRACY_VERSION=v0.13.1
RUN git clone --depth 1 --branch "${TRACY_VERSION}" https://github.com/wolfpld/tracy.git /tmp/tracy \
    && cmake -B /tmp/tracy/capture/build -S /tmp/tracy/capture -DCMAKE_BUILD_TYPE=Release -DNO_ISA_EXTENSIONS=ON \
    && cmake --build /tmp/tracy/capture/build -j"$(nproc)" \
    && cp /tmp/tracy/capture/build/tracy-capture /usr/local/bin/ \
    && cmake -B /tmp/tracy/csvexport/build -S /tmp/tracy/csvexport -DCMAKE_BUILD_TYPE=Release -DNO_ISA_EXTENSIONS=ON \
    && cmake --build /tmp/tracy/csvexport/build -j"$(nproc)" \
    && cp /tmp/tracy/csvexport/build/tracy-csvexport /usr/local/bin/ \
    && rm -rf /tmp/tracy

# Copy ASan suppressions file for Vulkan SDK/driver false-positives
COPY .asan_ignorefile /work/.asan_ignorefile

# Run as non-root user; callers should pass --user $(id -u):$(id -g) so that
# volume-mounted files are owned by the host user, not root.
RUN useradd --no-create-home --uid 1001 --shell /bin/bash ci

USER 1001
WORKDIR /work
