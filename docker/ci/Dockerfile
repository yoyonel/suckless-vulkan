FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    bash \
    ca-certificates \
    git \
    curl \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    libglfw3-dev \
    libglm-dev \
    libvulkan-dev \
    vulkan-tools \
    mesa-vulkan-drivers \
    glslang-tools \
    glslc \
    shaderc \
    xvfb \
    shellcheck \
    ccache \
    python3 \
    python3-pip \
    nodejs \
    npm \
    just \
    && rm -rf /var/lib/apt/lists/*

# uv provides uvx used by justfile recipes.
RUN pip3 install --break-system-packages --no-cache-dir uv

WORKDIR /work
