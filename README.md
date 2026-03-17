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

### HDR Assets

The skybox requires HDR environment maps in `assets/textures/hdr/`. These files are not tracked by git (large binaries). Copy or symlink your own Radiance `.hdr` files there before running:

```bash
mkdir -p assets/textures/hdr
cp /path/to/your/*.hdr assets/textures/hdr/
```

Any Radiance HDR equirectangular panorama works (e.g. from [Poly Haven](https://polyhaven.com/hdris)).

### Build and run

```bash
just help
just build
just run
```

Runtime controls:

- `Space`: pause/resume animation
- `Up`: increase animation speed
- `Down`: decrease animation speed
- `R`: reset animation time
- `C`: toggle mouse camera capture
- `W/A/S/D/Q/E`: move camera
- Mouse wheel: camera FOV
- `K`: toggle HDR skybox
- `PageUp` / `PageDown`: switch HDR envmap
- `Shift+PageUp` / `Shift+PageDown`: adjust envmap LOD
- `F11`: toggle fullscreen/windowed mode
- `Esc`: cleanly exit the application

The renderer now prefers an uncapped present mode when the Vulkan driver/compositor exposes one, and falls back automatically to a synchronized mode otherwise.

Logging:

- Structured logger enabled for app and tests (timestamp, pid/tid, tag, level).
- Log level can be configured with `VULKAN_LOG_LEVEL` (`DEBUG`, `INFO`, `WARNING`, `ERROR`, `CRITICAL`).
- Backward compatibility: `OGL_LOG_LEVEL` is also accepted.

### Run integration tests

```bash
just test
```

Targeted test flow:

- `just test-integration`: run only `EngineIntegrationTest`
- `just test-logic`: run only `LogicTests`
- `just test-all`: run `EngineIntegrationTest` then `LogicTests`

Coverage flow (unit/logical tests):

- `just coverage`: build with coverage flags, run `LogicTests`, generate reports
- Reports are written to `build/coverage/reports/` (`coverage.txt`, `coverage.xml`, `coverage.html`)

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
