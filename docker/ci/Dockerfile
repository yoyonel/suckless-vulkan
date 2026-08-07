FROM debian:bookworm-slim

ARG JUST_VERSION=1.46.0
ARG HADOLINT_VERSION=2.12.0
ARG ACTIONLINT_VERSION=1.7.4

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    bash=5.2.15-2+b10 \
    ca-certificates=20230311+deb12u1 \
    build-essential=12.9 \
    clang=1:14.0-55.7~deb12u1 \
    clang-tidy=1:14.0-55.7~deb12u1 \
    cmake=3.25.1-1 \
    curl=7.88.1-10+deb12u14 \
    libclang-rt-14-dev=1:14.0.6-12 \
    llvm=1:14.0-55.7~deb12u1 \
    ninja-build=1.11.1-2~deb12u1 \
    pkg-config=1.8.1-1 \
    libglfw3-dev=3.3.8-1 \
    libglm-dev=0.9.9.8+ds-6 \
    libvulkan-dev=1.3.239.0-1 \
    vulkan-tools=1.3.239.0+dfsg1-1 \
    mesa-vulkan-drivers=22.3.6-1+deb12u1 \
    glslang-tools=12.0.0-2 \
    xvfb=2:21.1.7-3+deb12u11 \
    xauth=1:1.1.2-1 \
    shellcheck=0.9.0-1 \
    ccache=4.8+really4.7.5-1 \
    python3=3.11.2-1+b1 \
    python3-pip=23.0.1+dfsg-1 \
    gdb=13.1-3 \
    && rm -rf /var/lib/apt/lists/*

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
    pymarkdownlnt==0.9.36

# Copy ASan suppressions file for Vulkan SDK/driver false-positives
COPY .asan_ignorefile /work/.asan_ignorefile

# Run as non-root user; callers should pass --user $(id -u):$(id -g) so that
# volume-mounted files are owned by the host user, not root.
RUN useradd --no-create-home --uid 1001 --shell /bin/bash ci

USER ci
WORKDIR /work
