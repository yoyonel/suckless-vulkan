# Color Pipeline Analysis: Legacy OpenGL vs Vulkan

## Legacy OpenGL (suckless-ogl) - Final Composite Pipeline

### 1. **Pipeline Order** (postprocess.frag)

```text
Scene (HDR: GL_RGBA16F)
  ↓
Motion Blur (optional, compute prepass)
  ↓
Bloom (optional, with mip selection)
  ↓
Depth of Field (optional)
  ↓
Chromatic Aberration (optional)
  ↓
Exposure Control (manual + auto-exposure)
  ↓
Color Grading (Saturation, Contrast, Gamma, Gain, Offset)
  ↓
Tone Mapping (Filmic Unreal-style)
  ↓
Vignette (optional)
  ↓
Gamma Correction (1/2.2)
  ↓
Banding (optional)
  ↓
Grain (optional)
  ↓
FXAA (optional)
  ↓
LDR Output (sRGB framebuffer)
```

### 2. **Tone Mapping Implementation** (tonemap.glsl)

**Function**: `unrealTonemap(vec3 x)`

```glsl
vec3 unrealTonemap(vec3 x)
{
    float a = 2.51 * tm_slope;
    const float b = 0.03;
    const float c = 2.43;
    float d = 0.59 * tm_shoulder;
    float e = 0.14 * (1.1 - tm_toe);

    vec3 res = (x * (a * x + b)) / (x * (c * x + d) + e);

    // Black clip: lifts blacks, removes crush
    if (tm_blackClip > 0.001) {
        res = max(vec3(0.0), res - tm_blackClip) / (1.0 - tm_blackClip);
    }

    // White clip: reduces highlights oversaturation
    if (tm_whiteClip > 0.001) {
        float maxVal = 1.0 - tm_whiteClip;
        res = min(vec3(maxVal), res) / maxVal;
    }

    return clamp(res, 0.0, 1.0);
}
```

**Parameters** (from postprocess.h):

- `tm_slope` (default: 1.0) — Controls overall contrast/brightness
- `tm_toe` (default: 0.0) — Lifts shadow details (0.0 = disabled)
- `tm_shoulder` (default: 0.0) — Compresses highlights (0.0 = disabled)
- `tm_blackClip` (default: 0.0) — Black level clipping (0.0 = disabled)
- `tm_whiteClip` (default: 0.0) — White level clipping (0.0 = disabled)

### 3. **Color Grading Implementation** (color_grading.glsl)

**Function**: `apply_color_grading(vec3 color)`

Steps in order:

1. **White Balance** (temperature + tint)
1. **Saturation** — blend between grayscale and saturated color
1. **Contrast** — expand/compress around 0.5
1. **Gamma** — power function (1.0 = disabled)
1. **Gain** — multiplicative brightness
1. **Offset** — additive brightness

**Parameters**:

- `cg_saturation` (default: 1.0) — 1.0 = normal, < 1.0 = desaturated, > 1.0 = more saturated
- `cg_contrast` (default: 1.0) — 1.0 = normal, < 1.0 = flatter, > 1.0 = more contrasty
- `cg_gamma` (default: 1.0) — 1.0 = linear, < 1.0 = darker, > 1.0 = brighter
- `cg_gain` (default: 1.0) — Multiplicative brightness
- `cg_offset` (default: 0.0) — Additive brightness

### 4. **Final Gamma Correction**

```glsl
color = pow(color, vec3(1.0 / 2.2));
```

Applied **after** tone mapping and color grading, converts linear→sRGB.

______________________________________________________________________

## Current State (Vulkan)

- ✅ HDR framebuffer (VK_FORMAT_R16G16B16A16_SFLOAT for offscreen)
- ✅ Swapchain: `VK_FORMAT_B8G8R8A8_SRGB` + `VK_COLOR_SPACE_SRGB_NONLINEAR_KHR`
- ✅ IBL baking and integration
- ✅ Material rendering with PBR
- ✅ `unrealTonemap()` present with matching defaults (slope=1.0, toe=0.0, etc.)
- ⚠️ **Manual `pow(x, 1/2.2)` in shader + automatic sRGB conversion = double gamma**

______________________________________________________________________

## Root Cause: Double Gamma Correction

### Legacy OpenGL

- `GL_FRAMEBUFFER_SRGB` is **NOT enabled** — no automatic conversion on write
- Gamma applied **once**, manually: `pow(color, 1/2.2)` in `postprocess.frag`
- Single correction: linear → sRGB ✅

### Vulkan (before fix)

- Swapchain is `VK_FORMAT_B8G8R8A8_SRGB` — hardware applies linear→sRGB on **every framebuffer write**
- Shader also applies `pow(color, 1/2.2)` manually
- Result: linear → pow(1/2.2) → pow(1/2.2) = **double correction**

```text
Linear value x = 0.5
  After manual gamma:    pow(0.5, 1/2.2) ≈ 0.729
  After hardware sRGB:   pow(0.729, 1/2.2) ≈ 0.861
  Expected single pass:  pow(0.5, 1/2.2) ≈ 0.729
```

Effect on output:

- **Highlights look washed out / crushed** (over-compressed)
- **Shadows lifted too aggressively** (overly bright)
- **Overall image looks pale/desaturated** compared to legacy

### Fix Applied

Remove `applyGammaCorrection()` from the shader. Let `VK_FORMAT_B8G8R8A8_SRGB` do the conversion automatically. Shader outputs linear values post tone-mapping.

```text
Pipeline after fix:
  IBL PBR  →  unrealTonemap()  →  outColor (linear)  →  [GPU sRGB store]  →  display
  ^--- shader ---^                                        ^--- hardware ---^
```

______________________________________________________________________

## Tone Mapping Parameter Equivalence

| Parameter | Legacy default | Vulkan `unrealTonemap()` |
|-----------|---------------|-------------------------|
| slope (`a`) | 1.0 → `a = 2.51` | `2.51 * 1.0 = 2.51` ✅ |
| b | 0.03 | `0.03` ✅ |
| c | 2.43 | `2.43` ✅ |
| shoulder (`d`) | 0.0 → `d = 0.0` | `0.59 * 0.0 = 0.0` ✅ |
| toe (`e`) | 0.0 → `e = 0.154` | `0.14 * 1.1 = 0.154` ✅ |
| blackClip | 0.0 (disabled) | 0.0 (disabled) ✅ |
| whiteClip | 0.0 (disabled) | 0.0 (disabled) ✅ |

______________________________________________________________________

## Integration Plan (Minimal)

### Phase 1: Tone Mapping Only

- Add basic `unrealTonemap()` to fragment shader
- Use defaults (all params = 0 for neutral response)
- Test if this brings legacy and Vulkan closer visually

### Phase 2: Gamma Correction

- Add `pow(color, vec3(1.0 / 2.2))` before framebuffer write
- Verify sRGB conversion matches legacy

### Phase 3: Color Grading (if needed)

- If differences persist, add saturation/contrast controls
- Start with neutral defaults, then tune

______________________________________________________________________

## Key Insight

The tone mapping function with **default parameters (slope=1.0, others=0.0)** is virtually a **linear passthrough** in the legacy. So if the legacy appears "normal," it means the tone mapping isn't doing much—the real difference might be:

1. **How the framebuffer is formatted** (sRGB vs linear)
1. **How gamma is applied** (early vs late, if at all)
1. **Exposure/brightness compensation** (auto-exposure in legacy?)

Test plan: Add simple tone mapping + gamma, compare side-by-side with legacy.
