# Billboard Raytraced Spheres

This document describes the implementation of the optimized billboard-based raytraced spheres in the Vulkan engine, which provides a high-quality alternative to the traditional triangulated icosphere mesh.

## Overview

Unlike standard meshes, billboards are procedural quads that are always oriented towards the camera. The fragment shader then performs a ray-sphere intersection to determine exactly which pixels belong to the sphere. This results in perfect curvature at any zoom level.

## Implementation Details

### 1. Procedural Quad Generation

To minimize CPU overhead and vertex buffer size, the billboard quads are generated procedurally in the vertex shader (`billboard.vert`) using `gl_VertexIndex`:

- **Vertex Input**: Only the instance buffer (per-sphere positions) is used.
- **Topology**: 6 vertices per instance form two triangles (a quad).
- **Coordinates**: $(-0.5, -0.5)$ to $(0.5, 0.5)$.

### 2. Exact Billboard Projection

A naive billboard (a simple screen-aligned quad) does not accurately represent a sphere's projected bounds, especially when viewed from the side or when the sphere is large relative to the screen.

We use an exact projection algorithm (ported from `projection_utils.glsl`) that calculates the tightest axis-aligned bounding box (AABB) in NDC space for a sphere.

- **Tangent Lines**: The projection logic finds the tangent lines from the camera position to the sphere to determine the exact min/max bounds.
- **Singularity Handling**: It correctly handles cases where part of the sphere is behind the camera or very close to the near plane.

### 3. Ray-Sphere Intersection

In the fragment shader (`billboard.frag`), we reconstruct the camera-space ray for each pixel and solve the quadratic equation:
$$||\\mathbf{ro} + t\\mathbf{rd} - \\mathbf{C}||^2 = r^2$$

- **Discard**: Pixels that don't intersect the sphere (discriminant $h < 0$) are discarded.
- **Depth**: We manually calculate `gl_FragDepth` by projecting the hit position back into clip space. This ensures correct depth testing against other objects in the scene.

### 4. Analytic Anti-Aliasing (AA)

To avoid jagged edges on the sphere's silhouette without requiring MSAA, we use an analytic approach based on the discriminant $h$:

1. **Metric**: $h = b^2 - c$ is the square of the "half-chord" length. It is 0 at the silhouette and increases towards the center.
1. **Pixel Size**: We calculate the world-space size of a pixel at the sphere's distance.
1. **Alpha Blending**: We use $h$ and the pixel size to calculate an `edgeFactor` (via `smoothstep`), which fades the edges of the sphere over a ~1-pixel width.

## Runtime Controls

The engine supports dynamic switching between the triangulated icosphere and the raytraced billboards:

- **Key 'B'**: Toggles between **BILLBOARD (Raytraced)** and **ICOSPHERE (Triangulated)** mode.
- **Default**: Billboards are the default rendering mode for optimal quality and performance.

## Shader Files

- `shaders/billboard.vert`: Quad generation and projection.
- `shaders/billboard.frag`: Raytracing, PBR shading, and analytic AA.
- `shaders/billboard_utils.glsl`: Shared projection utility functions.
