# Session Archive: VSync Implementation (Vulkan)
**Date**: 2026-03-20

## Context & Problem
- User experienced **screen tearing** during fast movement in the Vulkan engine (not present in the legacy OpenGL version).
- High FPS (>300) led to multiple frames per scanout cycle.

## Implemented Solution
1. **New VSync State**: Added `vsync` and `vsyncKeyWasDown` to `VulkanEngine`.
2. **Present Mode Selection**: 
   - **VSync ON**: Prefers `MAILBOX` (Fast Sync) if available, then `FIFO`. (Selected image count: 4 / Quad Buffering).
   - **VSync OFF**: Uses `IMMEDIATE`.
3. **Runtime Toggle**: 
   - Bound to key `V`.
   - Triggers a full swapchain recreation (`vk_recreate_swapchain`).
4. **Startup Configuration**:
   - Environment variable: `SVK_VSYNC=1` (enabled) or `0` (disabled).
   - CLI Arguments: `--vsync` or `--no-vsync`.
5. **Tooling**: Updated `justrun` recipe to accept arguments: `just run '--vsync'`.

## Observations during Testing
- **FIFO mode**: Successfully caps FPS to monitor refresh rate (60/120/144), but **tearing persists** in Fullscreen on user's NVIDIA/Linux (X11) setup.
- Tearing in `FIFO` despite 4 images suggests a driver/compositor bypass ("Allow Flipping").
- Potential workaround discussed: "Force Full Composition Pipeline" in `nvidia-settings`.
- Environment variable `__GL_SYNC_TO_VBLANK=1` was tested but the user opted to revert to "No VSync" by default for performance parity.

## Final Decision
- **Default VSync state**: **DISABLED** (Immediate) to prioritize raw performance and match legacy behavior.
- Toggle and configuration options remain fully functional for users who prefer Sync.

## Verification
- Succesfully verified via `just check` (Linting, Formatting, and Integration tests passed).
- Commited as: `chore(vsync): set VSync OFF by default and refine documentation/tooling`.
