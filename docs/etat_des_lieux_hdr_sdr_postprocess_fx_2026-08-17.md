# État des Lieux : Gestion HDR/SDR & Post-Processing FX (`suckless-vulkan`)

- **Date** : 17 Août 2026
- **Projet** : `suckless-vulkan`
- **Branche** : `feature/render-graph-integration`
- **Statut** : 📄 **Document de Référence & Spécification Technique**

______________________________________________________________________

## 1. Pipeline Global de Radiance : HDR vers SDR

Le moteur `suckless-vulkan` implémente un pipeline de rendu physique basé sur la séparation stricte entre l'évaluation de la **radiance linéaire non bornée (HDR)** et sa **compression d'affichage (SDR)**.

```text
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                                 PIPELINE GLOBAL HDR → SDR                              │
├────────────────────────────────────────────────────────────────────────────────────────┤
│                                                                                        │
│  [1. INGESTION ASSETS]                                                                 │
│     ├─ Fichiers Equirectangulaires .hdr (32-bit Float par canal)                       │
│     └─ Décodage stbi_loadf() ──► GPU Texture HDR (RGBA32F / RGBA16F)                   │
│                                                                                        │
│  [2. GÉOMÉTRIE & FORWARD PASS] (shader.frag, billboard.frag, skybox.frag)              │
│     ├─ Calcul PBR Cook-Torrance + IBL Split-Sum Multi-Scattering                       │
│     ├─ Énergie lumineuse linéaire pure non bornée : [0.0, +∞[                          │
│     └─ Écriture dans colorAttachment (RGBA16_SFLOAT / 65504 max)                       │
│                                                                                        │
│  [3. EXTRACTION BLOOM COMPUTE] (bloom_downsample.comp, bloom_upsample.comp)            │
│     ├─ Prélève l'énergie HDR brute > 1.0 (Seuil Soft-Knee Karis)                       │
│     └─ Pyramide 5 down mips + 4 up mips en B10G11R11_UFLOAT                            │
│                                                                                        │
│  [4. POST-PROCESSING MASTER] (postprocess.frag)                                        │
│     ├─ Composition Additive HDR : Scene_HDR + Bloom_HDR * Intensity                    │
│     ├─ Exposition : Color *= Exposure                                                  │
│     ├─ White Balance & Color Grading (Sat, Contrast, Gamma, Gain, Offset)              │
│     └─ Tonemapping Unreal ACES : Compression non-linéaire HDR [0, +∞[ ──► SDR [0.0, 1.0│
│                                                                                        │
│  [5. PRÉSENTATION MATÉRIELLE]                                                          │
│     └─ Écriture dans Swapchain (VK_FORMAT_B8G8R8A8_SRGB)                               │
│        └── Conversion Linéaire ──► Courbe sRGB par le Hardware GPU (0 coût shader)    │
│                                                                                        │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

### Table Récapitulative des Formats et Espaces Colorimétriques

| Étape | Ressource / Buffer | Format Vulkan | Espace Colorimétrique | Dynamique |
| :--- | :--- | :--- | :--- | :--- |
| **Ingestion IBL** | `envHdrImage` | `VK_FORMAT_R32G32B32A32_SFLOAT` | Linéaire HDR | $\[0.0, +\\infty\[$ |
| **Irradiance & Specular** | `irradianceMap`, `prefilterMap` | `VK_FORMAT_R16G16B16A16_SFLOAT` | Linéaire HDR | $[0.0, 65504.0]$ |
| **Rendu Scène** | `colorAttachment` | `VK_FORMAT_R16G16B16A16_SFLOAT` | Linéaire HDR | $[0.0, 65504.0]$ |
| **Bloom Pyramide** | `downMips[5]`, `upMips[4]` | `VK_FORMAT_B10G11R11_UFLOAT_PACK32` | Linéaire HDR | $[0.0, 65504.0]$ |
| **Affichage Écran** | `swapchainImages[3]` | `VK_FORMAT_B8G8R8A8_SRGB` | Non-Linéaire sRGB | $[0.0, 1.0]$ |

______________________________________________________________________

## 2. Inventaire Exhaustif des Effets (Post-Processing & FX)

### 🌟 FX 1 : Bloom Dual-Filtering (Jimenez / Karis)

- **Principe** : Extraction de l'énergie des zones sur-éclairées ($> 1.0$) et diffusion spatiale en double pyramide.
- **Shaders** : \[`shaders/bloom_downsample.comp`\](../shaders/bloom_downsample.comp) et \[`shaders/bloom_upsample.comp`\](../shaders/bloom_upsample.comp).
- **Détail technique** :
  - **Downsampling** : 5 passes Compute. Filtre Jimenez à 13 échantillons entrelacés + pondération Karis anti-firefly $w = \\frac{1}{1 + \\text{Luma}}$ + courbe continue Soft-Knee sur le premier niveau.
  - **Upsampling** : 4 passes Compute. Filtre tente $3\\times 3$ à 9 échantillons avec accumulation additive progressive.
  - **Optimisations** : Arithmétique SIMD native FP16 (`float16_t` / `f16vec3`), stockage `B10G11R11_UFLOAT` ($-50%$ bande passante DRAM), mode Fast-Path `SVK_BLOOM_QUARTER_RES=1`.
- **Temps GPU Tracy** : **$0.228\\text{ ms}$** (Half-Res) / **$0.189\\text{ ms}$** (Quarter-Res).
- **Contrôles clavier** :
  - `F7` : Activer / Désactiver Bloom.
  - `SHIFT+F7` : Mode Debug Bloom (isole la map de bloom pure à l'écran).
  - `Flèche Haut / Bas` : Intensité ($0.08$ par défaut).
  - `Flèche Gauche / Droite` : Rayon du filtre ($1.0$ par défaut).

### ☀️ FX 2 : Exposition Dynamique (Exposure)

- **Principe** : Multiplicateur linéaire d'énergie simulant le temps de pose ou l'ouverture du diaphragme de la caméra.
- **Formule** : $\\text{Color}\_{\\text{exp}} = \\text{Color} \\times \\text{Exposure}$.
- **Valeur par défaut** : $1.0$.
- **Contrôles clavier** :
  - `[` / `]` : Diminuer / Augmenter l'exposition.
  - `HOME` : Reset exposition à $1.0$.

### 🌡️ FX 3 : Balance des Blancs (White Balance)

- **Principe** : Correction de la température de couleur (Kelvin) et de la teinte (Tint vert/magenta) en espace linéaire.
- **Paramètres** :
  - `wbTemp` : Température en Kelvin ($6500\\text{ K}$ neutre, plage $[2000\\text{ K}, 15000\\text{ K}]$).
  - `wbTint` : Teinte vert/magenta ($0.0$ neutre, plage $[-1.0, +1.0]$).
- **Contrôles clavier** :
  - `F5` / `F6` : Ajuster température.
  - `SHIFT+F5` / `SHIFT+F6` : Ajuster teinte.

### 🎨 FX 4 : Color Grading Professionnel

- **Principe** : Ajustement colorimétrique 5 axes appliqué avant tonemapping :
  1. **Saturation** : $\\text{mix}(\\text{Luminance}, \\text{Color}, \\text{Saturation})$ ($\\text{Luma} = 0.2126R + 0.7152G + 0.0722B$).
  1. **Contraste** : $(\\text{Color} - 0.5) \\times \\text{Contraste} + 0.5$.
  1. **Gamma** : $\\text{Color}^{\\text{Gamma}}$.
  1. **Gain** : $\\text{Color} \\times \\text{Gain}$.
  1. **Offset** : $\\text{Color} + \\text{Offset}$.
- **Contrôles clavier** :
  - `{` / `}` : Saturation ($1.0$ neutre).
  - `(` / `)` : Contraste ($1.0$ neutre).
  - `-` / `=` : Gamma ($1.0$ neutre).
  - `<` / `>` : Gain ($1.0$ neutre).
  - `;` / `'` : Offset ($0.0$ neutre).

### 🎬 FX 5 : Tonemapper Unreal ACES Filmic

- **Principe** : Compression de la plage dynamique infinie $\[0.0, +\\infty\[$ vers la plage d'affichage LDR standard $[0.0, 1.0]$ avec courbe en S (préservation du contraste dans les sombres et roll-off doux des hautes lumières sans saturation brutale).
- **Formule ACES Narkowicz / Unreal** :
  $$\\text{Tonemap}(x) = \\text{clamp}\\left(\\frac{x \\cdot (2.51 \\cdot x + 0.03)}{x \\cdot (2.43 \\cdot x + 0.0) + 0.154}, 0.0, 1.0\\right)$$
- **Fichier** : \[`shaders/postprocess.frag`\](../shaders/postprocess.frag).

### 🔍 FX 6 : Anti-Aliasing Analytique Sous-Pixel (Sphères Billboards)

- **Principe** : Antialiasing géométrique parfait résolu analytiquement par lissage du discriminant de raymarching au bord de la sphère sans recours à un TAA ou FXAA.
- **Fichier** : \[`shaders/billboard.frag`\](../shaders/billboard.frag).
- **Formule** : $\\text{edgeFactor} = \\text{smoothstep}\\left(0.0, 1.0, \\frac{r^2 - d^2}{2r \\cdot \\text{pixelSizeWorld}}\\right)$.

### 🌐 FX 7 : Éclairage Physique IBL (Image-Based Lighting)

- **Principe** : Illumination globale environnementale basée sur Split-Sum Approximation (Karis / Epic Games).
- **Composantes** :
  - **Diffuse** : Carte d'irradiance convoluée pré-calculée en GPU Compute (`shaders/ibl_irmap.comp`).
  - **Spéculaire** : Carte d'environnement pré-filtrée par niveau de roughness (`shaders/ibl_spmap.comp`).
  - **BRDF LUT 2D** : Intégrale de Fresnel environnemental pré-calculée (`shaders/ibl_spbrdf.comp`).
  - **Multiple Scattering** : Compensation de perte d'énergie sur métaux rugueux (FScene multiple scattering $F\_{ms}$).
- **Contrôles clavier** :
  - `PAGE UP / PAGE DOWN` : Intensité IBL.
  - `INSERT / DELETE` : Mipmap LOD d'environnement.

### 🛠️ FX 8 : Vues Debug Dédiées (PBR & Shading Isolation)

- **Touches `0` à `8` & `SHIFT+F7`** :
  - `0` : Rendu PBR complet.
  - `1` : Albedo seul.
  - `2` : Normales monde.
  - `3` : Metallic.
  - `4` : Roughness.
  - `5` : Ambient Occlusion (AO).
  - `6` : Irradiance diffuse seule.
  - `7` : Réflexions spéculaires seules.
  - `8` : Table BRDF LUT 2D.
  - `SHIFT+F7` : Carte Bloom isolée.
