# 📝 Post-Mortem: IBL Vulkan Porting & Certification

This document summarizes the technical challenges and solutions encountered during the porting of the Image-Based Lighting (IBL) pipeline from legacy OpenGL to Vulkan.

## 🏁 Final Certification Status

The Vulkan implementation now aligns perfectly with the OpenGL reference across all metrics.

| File | MSE | SSIM | Status |
| :--- | :--- | :--- | :--- |
| **brdf_lut.hdr** | 0.000000 | 1.0000 | ✅ PASS |
| **irradiance.hdr** | 0.000053 | 0.9954 | ✅ PASS |
| **prefiltered_mip0.hdr** | 0.002568 | 0.9771 | ✅ PASS |

## 🛠️ Technical Challenges

### 1. Descriptor Architecture Structural Rigidity

**Challenge**: OpenGL's global state allows easy rebinding of textures and buffers. Vulkan requires strict, static `Descriptor Set Layouts`. Attempting to use a shared, generic layout for the multi-stage IBL pipeline led to binding type conflicts.

**Solution**: implemented a **specialized layout architecture**:

- `lum1Layout`: Sampler + Storage Buffer (Luminance Pass 1)
- `lum2Layout`: Storage Buffer + Storage Buffer (Luminance Pass 2)
- `iblLayout`: Sampler + Storage Image (Baking Pass)

This ensured type safety and prevented GPU memory access violations.

### 2. High-Resolution Resource Limits (4K)

**Challenge**: The reference 4K HDR assets pushed the compute group limits beyond the initial configuration. A 3840x2160 map requires ~32,400 groups (16x16 tiles). The initial `IBL_MAX_GROUPS = 16,384` caused silent out-of-bounds writes.

**Solution**: Increased `IBL_MAX_GROUPS` to `65,536`. This stabilized the luminance reduction, allowing the mean radiance to be correctly calculated for high-intensity maps.

### 3. Radiance Heuristics & Math Alignment

**Solution**:

- Synchronized the multiplier to `3.0x`.
- Implemented `soft_clamp_smoothstep` in Vulkan compute shaders.
- Aligned orientation (Y-flip) and face winding (frontFace) to match OGL world-space conventions. See [Coordinate Systems & Orientation](coordinate_systems.md) for full details and diagrams.

## 🧬 Infrastructure & Alignment

The `just verify-ibl` tool was critical. By creating a zero-pollution exporter in `suckless-ogl`, we could generate bit-exact reference data for comparison. This automated "Ground Truth" allowed us to iterate on the Vulkan implementation with absolute confidence.

## 🚀 Key Learning

In Vulkan, code porting is only 20% of the work. The remaining 80% is infrastructure synchronization (Layouts, Barriers, and Asset Loading) and numerical alignment of heuristics.
