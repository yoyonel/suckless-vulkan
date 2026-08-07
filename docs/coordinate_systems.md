# 🌐 Coordinate Systems & Orientation Conventions

Achieving "ISO" (identical) rendering between OpenGL and Vulkan requires more than just porting shader code. The two APIs have fundamentally different conventions for clip-space coordinates and triangle winding.

## 1. The NDC Y-Axis Discrepancy

- **OpenGL**: Normalized Device Coordinates (NDC) use a right-handed system where **+Y is up**.
- **Vulkan**: NDC use a system where **+Y is down** (matching window/screen coordinates).

To maintain the same world-space "Up" vector (+Y) and avoid an upside-down image, we apply a **Y-flip** in the projection matrix:

```cpp
// vk_engine_frame.cpp
proj[1][1] *= -1; 
```

## 2. Face Winding & Culling (The "Hidden" Challenge)

The Y-flip has a side-effect: it **inverts the apparent winding order** of primitives in clip space.

### The Problem

1. Geometry is defined with **Counter-Clockwise (CCW)** winding for outer faces in world space.
1. The Y-flip mirrors the geometry vertically, turning CCW triangles into **Clockwise (CW)** triangles in screen space.
1. If the pipeline is set to `VK_FRONT_FACE_COUNTER_CLOCKWISE` with `BACK_BIT` culling:
   - The outer faces (now CW) are culled.
   - The **inner faces** (originally CW, now CCW) are rendered instead.

### The Symptom: Inverted Normals

Rendering inner faces causes the normals (calculated from geometry or positions) to point **towards the center** of objects. In debug mode (`N * 0.5 + 0.5`), this manifests as inverted/complementary colors (e.g., Yellow instead of Magenta).

### The Solution: Correcting frontFace

To reach ISO parity with OpenGL while keeping the Y-flip, we must tell Vulkan that **Clockwise** is the correct front-face winding for the rasterizer:

```cpp
// vk_engine_init.cpp
rs.frontFace = VK_FRONT_FACE_CLOCKWISE; // Compensates for Y-flip
```

## 3. Transformation Pipeline Comparison

The following diagram illustrates how the orientation is preserved across the pipeline:

```mermaid
graph TD
    subgraph "Legacy OpenGL (Reference)"
    OGL_W[World: +Y Up, CCW] --> OGL_NDC[NDC: +Y Up]
    OGL_NDC --> OGL_RAS[Rasterizer: CCW=Front]
    OGL_RAS --> OGL_OUT[Outer Shell Visible ✅]
    end

    subgraph "Vulkan (Initial Mismatch)"
    VK_W[World: +Y Up, CCW] --> VK_NDC[NDC: +Y Down]
    VK_NDC --> VK_FF[Rasterizer: CCW=Front]
    VK_FF --> VK_ERR[Inner Shell Visible ❌]
    end

    subgraph "Vulkan (ISO Aligned)"
    VKF_W[World: +Y Up, CCW] --> VKF_FLIP[NDC: +Y Up via Y-Flip]
    VKF_FLIP --> VKF_FF[Rasterizer: CW=Front]
    VKF_FF --> VKF_RES[Outer Shell Visible ✅]
    end
```

## 4. Visual Parity Check (Normals)

With the alignment fixed, the world-space normals are correctly oriented, leading to identical debug visualizations:

| Feature | OpenGL (Reference) | Vulkan (Aligned) |
| :--- | :--- | :--- |
| **Top Normal (+Y)** | Green (0.5, 1.0, 0.5) | Green (0.5, 1.0, 0.5) |
| **Right Normal (+X)** | Red (1.0, 0.5, 0.5) | Red (1.0, 0.5, 0.5) |
| **Front Normal (+Z)** | Blue (0.5, 0.5, 1.0) | Blue (0.5, 0.5, 1.0) |

> [!IMPORTANT]
> Always verify the `frontFace` setting whenever a change is made to the projection matrix or the coordinate system conventions, as it is the most common source of "inverted" or "missing" geometry in Vulkan ports.
