# Vulkan Base

Lightweight Vulkan rendering base in C++17, with robust local tooling and CI/CD automation.

> Badge links target the GitHub repository `yoyonel/suckless-vulkan`.

[![CI](https://github.com/yoyonel/suckless-vulkan/actions/workflows/ci.yml/badge.svg)](https://github.com/yoyonel/suckless-vulkan/actions/workflows/ci.yml)
[![Release Binaries](https://github.com/yoyonel/suckless-vulkan/actions/workflows/release.yml/badge.svg)](https://github.com/yoyonel/suckless-vulkan/actions/workflows/release.yml)
[![Latest Release](https://img.shields.io/github/v/release/yoyonel/suckless-vulkan?display_name=tag)](https://github.com/yoyonel/suckless-vulkan/releases)
[![Release Date](https://img.shields.io/github/release-date/yoyonel/suckless-vulkan)](https://github.com/yoyonel/suckless-vulkan/releases)
[![Last Commit](https://img.shields.io/github/last-commit/yoyonel/suckless-vulkan)](https://github.com/yoyonel/suckless-vulkan/commits/master)

## What This Project Is

This repository is a Vulkan-based rendering engine foundation focused on:

- Explicit Vulkan lifecycle management (instance, device, swapchain, render pass, pipeline)
- GPU memory management through VMA
- Headless-compatible integration testing (frame rendering + swapchain readback)
- Strong local developer tooling (format, lint, docs checks, pre-commit/pre-push gates)
- GitHub Actions CI/CD for build, tests, and binary release artifacts

## Tech Stack

- C++17
- Vulkan 1.x
- GLFW
- GLM
- VMA (AMD Vulkan Memory Allocator)
- CMake + just

## Repository Layout

```text
.
|- src/                 Engine implementation
|- tests/               Integration test binary
|- shaders/             GLSL shaders and generated SPIR-V
|- ext/                 Third-party dependencies (vma, stb)
|- scripts/             Utility scripts (headless test runner)
|- docs/                MkDocs documentation pages
|- .github/workflows/   CI/CD pipelines
|- CMakeLists.txt       Build definition
`- justfile             Developer task runner
```

## Quick Start

### Prerequisites (Linux)

- `cmake`
- `clang-format`, `clang-tidy`
- `glslangValidator` (obligatoire), `glslc` (recommande)
- Vulkan runtime + headers (`libvulkan-dev`, drivers)
- `glfw3`, `glm`

### Build and run

```bash
just build
just run
```

### Run integration tests

```bash
just test
```

To force saving a rendered frame:

```bash
VULKAN_TEST_SAVE_FRAME=1 just test
```

Output file is generated at repository root: `test_output.png`.

## Quality Gates

Main commands:

- `just format` -> format owner files (code, docs, yaml, shell, cmake, justfile)
- `just lint` -> lint owner files
- `just check` -> format + lint + tests

Local Git hooks:

```bash
just pre-commit-install
```

This installs:

- `pre-commit` hook: `just format` + `just lint-fast`
- `pre-push` hook: `just test`

Full local gate (equivalent to CI quality checks):

```bash
just lint && just test
```

## Memory Safety

### CPU/RAM: AddressSanitizer + UndefinedBehaviorSanitizer

Detect heap corruption, buffer overflows, use-after-free, and undefined behavior:

```bash
just build-asan
just test-asan
```

### GPU/VRAM: Vulkan Validation Layers

Detect Vulkan API misuse and synchronization errors:

```bash
just test-validation-layers
```

See `docs/tooling.md` for detailed memory safety diagnostics.

## Development Model

This project practices **Trunk-Based Development (TBD)**:

- Single `master` branch for all development
- Short-lived feature branches (1–3 days) for code review
- Direct commits to `master` for small changes (small teams)
- Build never breaks — pre-commit/pre-push gates prevent this
- Releases tagged directly from `master` (no long-lived release branches)

For detailed workflow, feature flags, hotfix strategy, and commit conventions, see [.github/DEVELOPMENT.md](.github/DEVELOPMENT.md).

## CI/CD

- `ci.yml`: runs on push/PR to `master`, builds and tests `Release` and `Debug`
- `ci-image.yml`: builds/publishes the CI Docker image used by `ci.yml`
- `release.yml`: runs on tags `v*`, packages and uploads debug/release tarballs as release assets

CI runs inside a dedicated Docker image, and the same flow can be reproduced locally:

```bash
just ci-docker-all
```

See full operational details in `docs/ci_cd.md`.

## Documentation

Serve docs locally:

```bash
just docs-uv-serve
```

Build docs:

```bash
just docs-uv-build
```
