# Comparatif des Effets (FX) & Post-Processing : `suckless-ogl` vs `suckless-vulkan`

- **Date** : 17 Août 2026
- **Projets Analysés** : `suckless-ogl` et `suckless-vulkan`
- **Statut** : 📄 **Document d'Analyse Comparative & Roadmap de Parité**

______________________________________________________________________

## 1. État des Lieux des Effets dans le Projet Mère (`suckless-ogl`)

Dans `suckless-ogl`, le post-processing est orchestré par un shader fragment maître modulaire (`shaders/postprocess.frag`) incluant **15 sous-modules** spécialisés :

```text
Ordre d'Exécution du Pipeline suckless-ogl :
[Scene Color FBO] + [Depth FBO] + [Stencil FBO]
       │
       ▼
 1. Motion Blur (avec TileMax & NeighborMax Compute passes)
 2. Chromatic Aberration (masqué sur Skybox via Stencil)
 3. FXAA (Fast Approximate Anti-Aliasing, skippé sur Skybox)
 4. Depth of Field (DoF Bokeh avec Circle of Confusion)
 5. Bloom Dual-Filtering (Mélange additif)
 6. Atmospheric Fog (Exponentiel avec Height Falloff en espace HDR)
 7. Exposure & Auto-Exposure Adaptation (Compute histogram / 1x1 R32F)
 8. Color Grading 5-Axes & White Balance
 9. 3D LUT Gamut Mapping (Texture 3D 32x32x32)
10. Tonemapping (Unreal / ACES / Reinhard)
11. Vignette (SDF Rounded Box / Aspect Ratio correct)
12. Gamma Correction (pow(color, 1.0/2.2))
13. Banding / Quantization / Dithering (Style Rétro / Précision 8-bit)
14. Film Grain (Bruit dynamique pseudo-aléatoire pondéré par la luminance)
       │
       ▼
[Framebuffer Final OpenGL]
```

______________________________________________________________________

## 2. Matrice Comparative Exhaustive (`suckless-ogl` vs `suckless-vulkan`)

| Effet / Fonctionnalité | Présent dans `suckless-ogl` | Présent dans `suckless-vulkan` | Différence d'Implémentation & Stratégie |
| :--- | :---: | :---: | :--- |
| **Bloom Dual-Filtering** | ✅ Oui | ✅ Oui | ⚡ **Majeure** : OGL = Fragment Shaders + FBOs FP32. VK = GPU Compute natif FP16 (`float16_t`), stockage `B10G11R11_UFLOAT`, Soft-Knee unifié, Quarter-Res fast-path ($0.189\\text{ ms}$). |
| **Exposition Manuelle** | ✅ Oui | ✅ Oui | **Identique** : Multiplicateur linéaire $\\text{Color} \\times \\text{Exposure}$. |
| **Auto-Exposition Dynamique** | ✅ Oui | ❌ Non | **Absence VK** : OGL calcule la luminance moyenne par réduction compute (`lum_downsample.comp` + `lum_adapt.comp` avec delta-time). |
| **White Balance (Kelvin/Tint)** | ✅ Oui | ✅ Oui | **ISO Paritaire** : Même formule de décalage thermique $[2000\\text{ K}, 15000\\text{ K}]$ et teinte $[-1, 1]$. |
| **Color Grading 5-Axes** | ✅ Oui | ✅ Oui | **ISO Paritaire** : Saturation, Contraste, Gamma, Gain, Offset en espace linéaire. |
| **Tonemapping ACES Unreal** | ✅ Oui | ✅ Oui | **ISO Paritaire** : Même courbe Narkowicz / Unreal Engine. |
| **Gestion Gamma (sRGB)** | ✅ Oui (Shader) | ✅ Oui (Hardware) | ⚡ **Majeure** : OGL = `pow(color, 1.0/2.2)` dans le shader. VK = Conversion gamma automatique matérielle par la Swapchain `VK_FORMAT_B8G8R8A8_SRGB`. |
| **Motion Blur (Flou de Mouvement)**| ✅ Oui | ❌ Non | **Absence VK** : OGL a un pipeline complet (Velocity Buffer + `tile_max_velocity.comp` + `neighbor_max_velocity.comp` + Reconstruction). |
| **FXAA (Anti-Aliasing)** | ✅ Oui | ❌ Non | **Absence VK** : OGL applique FXAA 3.11 sur les objets (isolés par stencil). VK utilise l'antialiasing analytique géométrique sous-pixel sur les billboards. |
| **Depth of Field (DoF Bokeh)** | ✅ Oui | ❌ Non | **Absence VK** : OGL gère CoC (Circle of Confusion) et flou focal basé sur la depth. |
| **Atmospheric Fog (Brouillard)** | ✅ Oui | ❌ Non | **Absence VK** : OGL intègre un brouillard exponentiel avec dégradé d'altitude en espace HDR. |
| **Vignette Cinématique** | ✅ Oui | ❌ Non | **Absence VK** : OGL utilise une fonction SDF boîte arrondie paramétrable (Roundness, Smoothness, Color). |
| **Film Grain (Grain Pellicule)** | ✅ Oui | ❌ Non | **Absence VK** : OGL ajoute un bruit haute-fréquence pondéré par la luminance pour casser les aplats. |
| **3D LUT (Look-Up Table 3D)** | ✅ Oui | ❌ Non | **Absence VK** : OGL supporte l'étalonnage cinématographique par texture 3D (`lut3d.glsl`). |
| **Banding / Dithering** | ✅ Oui | ❌ Non | **Absence VK** : OGL propose une quantification stylisée des niveaux de couleur. |
| **Chromatic Aberration** | ✅ Oui | ❌ Non | **Absence VK** : OGL applique une séparation RGB radiale avec dispersion optique. |
| **Shockwave & Particle Trails** | ✅ Oui | ❌ Non | **Absence VK** : Shaders dédiés `shockwave.frag` et `trail.frag` dans OGL. |
| **Spherical Harmonics GI Probes** | ✅ Oui | ❌ Non | **Absence VK** : OGL a un système de sondes SH 1-bounce diffuse (`sh_probe.glsl`). |

______________________________________________________________________

## 3. Analyse Approfondie des Divergences Stratégiques

### 3.1. Stratégie du Bloom : Compute Moderne vs Ping-Pong FBO

- **`suckless-ogl`** : Chaque passe de downsample/upsample est un drawcall de quad plein écran exécutant un Fragment Shader qui écrit dans un FBO GL. Utilise le format FP32 et nécessite une passe de prefilter distincte (`bloom_prefilter.frag`).
- **`suckless-vulkan`** : 100% Compute GPU (`vkCmdDispatch`).
  - Zéro overhead de rasterizer (pas de pipeline graphique, pas de VBO quad).
  - Écriture directe via `imageStore` en mémoire `B10G11R11_UFLOAT` (moitié moins de bande passante).
  - Arithmétique vectorielle SIMD FP16 (`f16vec3`) native exécutée 2x plus vite sur les ALUs GPU.
  - Le préfiltrage Soft-Knee et la pondération Karis sont fusionnés dans le premier dispatch de downsample.

### 3.2. Stratégie Gamma & Espaces Colorimétriques

- **`suckless-ogl`** : Travaille sur un framebuffer de présentation `GL_RGBA8` (non-sRGB). Le shader doit donc exécuter explicitement `pow(color, vec3(1.0 / 2.2))` à la ligne 139 de `postprocess.frag`.
- **`suckless-vulkan`** : Utilise une surface swapchain `VK_FORMAT_B8G8R8A8_SRGB`. La conversion linéaire $\\to$ sRGB est exécutée gratuitement par les unités d'écriture du GPU. Tout `pow()` dans le shader fragment conduirait à un double gamma (image délavée).

### 3.3. Stratégie d'Isolation (Stencil Buffer vs Graphe de Passes)

- **`suckless-ogl`** : Utilise le Stencil Buffer matériel (`GL_STENCIL_TEST`) lors du rendu des objets pour marquer le fond ($0$) vs les sphères ($1$). Dans `postprocess.frag`, le stencil est lu (`usampler2D stencilTexture`) pour désactiver le Motion Blur, le FXAA et l'Aberration Chromatique sur le ciel étoilé.
- **`suckless-vulkan`** : Ne génère pas de texture de stencil pour le post-process. L'isolation et les transitions sont résolues au niveau du RenderGraph et des masques de passes.

______________________________________________________________________

## 4. Bilan & Potentiel de Portage pour `suckless-vulkan`

### Effets "Quick Wins" faciles à porter dans `shaders/postprocess.frag`

Ces effets n'ont besoin d'aucune passe supplémentaire ni de buffer auxiliaire :

1. **Vignette Cinématique** : ~20 lignes de GLSL (SDF distance).
1. **Film Grain** : ~15 lignes de GLSL (fonction de hash pseudo-aléatoire + luminance).
1. **Atmospheric Fog** : Nécessite seulement d'échantillonner `depthImage` (déjà disponible) en entrée du post-process.
1. **Chromatic Aberration** : Décalage radial simple des canaux R et B.

### Effets Avancés nécessitant une infrastructure de passes dédiée

1. **Auto-Exposure (Luminance Adaptation)** : Nécessite 2 dispatches Compute de réduction de luminance avant le post-process.
1. **Motion Blur** : Nécessite un Velocity Buffer (calculé lors du forward pass avec `prevModelViewProj`), puis 2 dispatches compute (`TileMax` / `NeighborMax`).
1. **Depth of Field (DoF)** : Nécessite un calcul de Cercle de Confusion et un filtrage étendu avec séparation foreground/background.
