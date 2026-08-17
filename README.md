# Suckless Vulkan

Lightweight, high-performance Vulkan rendering engine in C++17, featuring a declarative **RenderGraph**, modern **RHI with BindGroups**, **Dual-Filtering Bloom**, **Temporal 64-Bin Auto-Exposure**, **Async IBL**, zero-allocation frame recording, and automated profiling (Tracy, Intel VTune, Heaptrack).

> Badge links target the GitHub repository `yoyonel/suckless-vulkan`.

[![CI](https://github.com/yoyonel/suckless-vulkan/actions/workflows/ci.yml/badge.svg)](https://github.com/yoyonel/suckless-vulkan/actions/workflows/ci.yml)
[![Release Binaries](https://github.com/yoyonel/suckless-vulkan/actions/workflows/release.yml/badge.svg)](https://github.com/yoyonel/suckless-vulkan/actions/workflows/release.yml)
[![Latest Release](https://img.shields.io/github/v/release/yoyonel/suckless-vulkan?display_name=tag)](https://github.com/yoyonel/suckless-vulkan/releases)
[![Release Date](https://img.shields.io/github/release-date/yoyonel/suckless-vulkan)](https://github.com/yoyonel/suckless-vulkan/releases)
[![Last Commit](https://img.shields.io/github/last-commit/yoyonel/suckless-vulkan)](https://github.com/yoyonel/suckless-vulkan/commits/dev)

## Features & Architecture

- **Declarative RenderGraph**: Automatic DAG dependency analysis, topological pass sorting (Kahn algorithm), transient image memory management, and automatic `VkImageMemoryBarrier` transition batching with multi-frame layout persistence.
- **Modern RHI & Declarative BindGroups**: Clean abstraction layer over Vulkan 1.x with declarative graphics and compute pipeline descriptors, automatic descriptor set layout caching (`VulkanDescriptorCache`), and zero dynamic allocations per frame.
- **Unified Post-Process DAG**: Single execution graph connecting `ForwardPass` → `AutoExposure (Histogram + Adapt)` → `Bloom (Downsample x5 + Upsample x4)` → `PostProcess (Tonemapping + Color Grading)`.
- **Dual-Filtering Compute Bloom**: Jimenez 13-tap downsample filter with Karis anti-firefly weighting and 9-tap 3x3 Tent upsample filter. Utilizes 32-bit `B10G11R11_UFLOAT` storage images and native `float16_t` FP16 arithmetic ($0.228\text{ ms}$ at 1080p, $-71\%$ GPU time).
- **Temporal 64-Bin LDS Auto-Exposure**: Compute histogram binning in Local Data Share (LDS), percentile luminance metering ($5\% - 98\%$), shadow cut-off cut, and asymmetric temporal adaptation with a real-time procedural 64-bar debug histogram HUD (`SHIFT+F8`).
- **Asynchronous IBL & HDR Streaming**: Background async computation of Irradiance Maps, Specular Prefilter Maps, and BRDF LUT, backed by fast-path KTX2/Zstandard binary cache.
- **Zero-Allocation Hot-Path**: Strict 0 heap allocation policy during frame recording (`vk_draw_frame_internal`), delivering $745+\text{ FPS}$ throughput.
- **Advanced Profiling Suite**: Fully integrated support for **Tracy Profiler** (GPU/CPU timeline), **Intel VTune Profiler** (hotspots, memory access, cache misses, DRAM bound), and **Heaptrack** (heap allocation tracking).
- **Headless Testing & CI/CD**: Deterministic visual regression testing across 9 golden references, synthetic GPU compute tests, LLVM code coverage, ASan/UBSan, and Vulkan Validation Layers.

## Tech Stack

- **Language**: C++17 (with strict warnings and `-Werror`)
- **Graphics API**: Vulkan 1.x
- **Memory Management**: AMD Vulkan Memory Allocator (VMA)
- **Windowing & Math**: GLFW 3, GLM
- **Asset Loading**: stb_image, KTX2 / Zstandard
- **Build System & Tooling**: CMake, `just`, Clang-Tidy, Clang-Format, pymarkdown, glslangValidator

## Repository Layout

```text
.
|- src/                 Engine implementation
|  |- rhi/              Render Hardware Interface (RHI, RenderGraph, CommandList, DescriptorCache)
|  |- shaders/          GLSL source shaders (Graphics & Compute) and compiled SPIR-V
|- tests/               Integration tests, unit tests, and RenderGraph stress tests
|- assets/              Textures, HDR environment maps, and 3D models
|- scripts/             Automated profiling, benchmark runners, and verification scripts
|- docs/                MkDocs documentation and architectural decision records (ADR)
|- .github/workflows/   CI/CD pipelines (8-job matrix, ASan, Validation Layers, Tracy, Coverage)
|- CMakeLists.txt       Build definition
`- justfile             Developer task runner
```

## Quick Start

### Prerequisites (Linux)

- `cmake` (>= 3.20)
- `clang-format`, `clang-tidy`
- `glslangValidator` (required), `glslc` (recommended)
- Vulkan runtime & headers (`libvulkan-dev`, Mesa / Intel / NVIDIA drivers)
- `glfw3`, `glm`

### HDR Assets

The engine uses Radiance `.hdr` environment maps in `assets/textures/hdr/` (not tracked in git due to file size). Place your own equirectangular `.hdr` files there:

```bash
mkdir -p assets/textures/hdr
cp /path/to/your/*.hdr assets/textures/hdr/
```

Any Radiance HDR equirectangular panorama works (e.g., from [Poly Haven](https://polyhaven.com/hdris)).

### Build and Run

```bash
just help       # List all available developer recipes
just build      # Build Release configuration
just run        # Run the application
```

### Runtime Controls

| Key | Action |
| :--- | :--- |
| `W` / `A` / `S` / `D` / `Q` / `E` | Camera movement (Forward, Left, Back, Right, Down, Up) |
| `C` / Right Mouse | Toggle mouse camera capture |
| Mouse Wheel | Adjust camera Field of View (FOV) |
| `Space` | Pause / resume scene animation |
| `Up` / `Down` | Increase / decrease animation playback speed |
| `R` | Reset animation timer |
| `K` | Toggle HDR Skybox visibility |
| `PageUp` / `PageDown` | Cycle through HDR environment maps (triggers async IBL bake) |
| `Shift+PageUp` / `Shift+PageDown` | Adjust environment map mip level (LOD) |
| `F5` | Dynamic hot-reload of RHI rendering backend |
| `F6` | Toggle IBL debug mode (Irradiance map visualization) |
| `F7` | Toggle Bloom debug mode |
| `Shift+F8` | Toggle real-time Auto-Exposure 64-bar histogram debug HUD |
| `V` | Toggle Vertical Synchronization (VSync) |
| `F11` | Toggle Fullscreen / Windowed mode |
| `Esc` | Cleanly exit application |

### Environment Variables & CLI Options

- `SVK_VSYNC=1` / `--vsync`: Enable VSync at startup (default: off / `--no-vsync`).
- `SVK_BLOOM_QUARTER_RES=1`: Enable Quarter-Resolution Bloom pass ($480\times270$ at 1080p, $-44\%$ compute time).
- `VULKAN_LOG_LEVEL=DEBUG`: Set log verbosity level (`DEBUG`, `INFO`, `WARN`, `ERROR`).
- `SVK_UPDATE_REFERENCES=1`: Update visual regression reference images during test run.

---

## Profiling & Benchmarking Suite

### 1. Tracy Profiler (GPU & CPU Timelines)

```bash
just build-tracy                # Compile with Tracy instrumentation
just run-tracy                  # Run application connected to Tracy
just test-integration-tracy     # Run headless automated trace capture
just verify-tracy-trace         # Validate GPU/CPU execution timeline invariants
```

### 2. Intel VTune Profiler (Microarchitecture & Memory)

```bash
just profile-vtune-hotspots     # CPU hotspots and top C++ functions
just profile-vtune-memory       # Memory access, L1/L2/LLC cache misses, DRAM bound
just profile-vtune-threading    # Thread contention, lock wait times, oversubscription
just profile-vtune-gui          # Open interactive VTune GUI on latest capture
```

### 3. Heaptrack (RAM & Dynamic Allocations)

```bash
just profile-heaptrack          # Profile heap allocations and verify 0 alloc/frame
```

---

## Testing & Quality Gates

### Integration & Unit Tests

```bash
just test              # Run full test suite
just test-integration  # Run engine integration & visual comparison test
just test-logic        # Run logic and algorithmic unit tests
just test-all          # Run integration tests + logic tests sequentially
```

### Code Coverage

```bash
just coverage          # Build with LLVM coverage instrumentation and generate HTML report
```

### Memory Safety & Diagnostics

```bash
just test-asan                 # Run AddressSanitizer + UndefinedBehaviorSanitizer
just test-validation-layers    # Run with Vulkan Validation Layers enabled
```

### Quality Checks & Git Hooks

```bash
just format             # Format all C++, Shaders, CMake, Shell, YAML, Python, Markdown
just lint               # Run all linters (Clang-Tidy, glslangValidator, ShellCheck, etc.)
just check              # Full local gate (format + lint + test-all)
just pre-commit-install # Install pre-commit (lint) and pre-push (test) Git hooks
```

---

## CI/CD Architecture

The GitHub Actions CI/CD matrix executes across 8 parallel jobs:

- **`static-checks`**: Lint Dockerfile (`hadolint`) and GitHub Actions workflows (`actionlint`).
- **`lint`**: Clang-Tidy, CMake-lint, ShellCheck, Yamllint, glslangValidator, pymarkdown, Ruff.
- **`memory-checks-asan`**: ASan + UBSan memory corruption and leak checks.
- **`memory-checks-validation-layers`**: Vulkan Validation Layers validation.
- **`coverage`**: LLVM source-based coverage report generation.
- **`build-tracy`**: Headless Tracy integration test and timeline invariant verification.
- **`build-and-test (Debug)`**: Compilation and visual regression tests in Debug.
- **`build-and-test (Release)`**: Compilation and visual regression tests in Release.

Local Docker reproduction:

```bash
just ci-docker-all
```

---

## Documentation

- **Live Documentation Server**: `just docs-uv-serve` (served via MkDocs Material).
- **Static Documentation Build**: `just docs-uv-build`.
- **Architectural Plans & ADRs**: Located in [`docs/`](docs/).
