# État de l'Art : Techniques d'Implémentation du Bloom dans les Moteurs AAA (2026-08-17)

## 1. Contexte & Enjeux du Post-Processing HDR

Dans les moteurs de jeux modernes, le **Bloom** simule la diffusion de la lumière à travers la cornée humaine ou les lentilles d'un appareil photographique lorsqu'une zone dépasse la plage dynamique normale (*High Dynamic Range*).

Sur GPU, le défi réside dans le compromis entre :

1. **Qualité Visuelle & Stabilité Temporelle** : Éviter les artefacts de coupure dure (*hard cutoff*), le scintillement des sous-pixels (*specular fireflies*), et les déformations en damier (*box filter artifacts*).
1. **Bande Passante Mémoire & Cache L2/DRAM** : Traiter des millions d'échantillons de texture sans saturer le bus mémoire, particulièrement sur les architectures à mémoire unifiée (**UMA** / GPU intégrés Intel Iris Xe, Apple Silicon, APU AMD).
1. **Overhead CPU/GPU & Barrières Pipeline** : Minimiser le nombre de dispatches compute et de barrières d'exécution/synchronisation (`vkCmdPipelineBarrier`).

______________________________________________________________________

## 2. Taxonomie Complète des Méthodes de l'Industrie

```text
┌─────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                   TECHNIQUES DE BLOOM MODERNE                                   │
├──────────────────────────────┬──────────────────────────────┬───────────────────────────────────┤
│ 1. Dual-Filtering Pyramid    │ 2. Single-Pass (AMD SPD)     │ 3. Spectral FFT Convolution       │
│ (Jimenez / Karis / UE4-UE5)  │ (FidelityFX / id Tech 7)     │ (UE5 Physical / CryEngine)        │
│ ──────────────────────────── │ ──────────────────────────── │ ───────────────────────────────── │
│ • 13-Tap Downsample (Karis)  │ • 1 seul dispatch compute    │ • Convolution espace de Fourier   │
│ • 9-Tap 3x3 Tent Upsample    │ • Atomiques + LDS inter-mips │ • Diffractions de lentille / Bokeh│
│ • Rendu organique et stable  │ • Zéro barrière GPU / DRAM   │ • Coût indépendant du rayon flou │
├──────────────────────────────┼──────────────────────────────┴───────────────────────────────────┤
│ 4. Subgroup/Wave Intrinsics  │ 5. Separable Multi-Pass Gaussian (Legacy DX11/GL)                │
│ (Vulkan 1.3 / SIMD Wave)     │ (CryEngine 3 / Source Engine / Ping-Pong 1D H+V)                 │
│ • Registres ALUs (Quad Swap) │ • Multiples passes Ping-Pong horizontales + verticales           │
│ • Zéro trafic LDS / RAM      │ • Gourmand en bande passante et passes de frame                  │
└──────────────────────────────┴───────────────────────────────────────────────────────────────────┘
```

______________________________________________________________________

## 3. Détail des Approches Industrielles

### 3.1 Méthode 1 : Dual-Filtering Pyramid (Marius / Jimenez / Karis)

- **Origine & Adoption** : Présenté par Jorge Jimenez (SIGGRAPH 2014, *Call of Duty: Advanced Warfare*), raffiné par Brian Karis (Epic Games, *Unreal Engine 4*), adopté par Unity HDRP et Godot 4.
- **Fonctionnement** :
  - **Downsampling** : 5 mips consécutifs. Échantillonnage Jimenez 13-tap (5 bilinéaires combinés) avec filtrage *Soft-Knee* et moyenne pondérée de Karis sur Mip 0 pour éradiquer les étincelles spéculaires.
  - **Upsampling** : Remontée pyramidale avec filtre tente 3x3 (9-tap) et blending additif progressif.
- **Bilan** :
  - *Forces* : Qualité visuelle exceptionnelle, stabilité temporelle, simple à déboguer.
  - *Faiblesses* : $2N-1$ passes (9 dispatches compute) avec barrières séquentielles.

### 3.2 Méthode 2 : AMD FidelityFX Single-Pass Downsampler (SPD)

- **Origine & Adoption** : AMD GPUOpen SDK, moteurs Frostbite (DICE), id Tech 7 (*Doom Eternal*).
- **Fonctionnement** :
  - Génère l'intégralité d'une chaîne pyramidale (jusqu'à 12 mips) en **un seul `vkCmdDispatch`**.
  - S'appuie sur la mémoire locale partagée (**LDS**) et un compteur atomique global (`atomicAdd`).
  - Le dernier Workgroup de chaque passe fusionne les résultats pour les niveaux supérieurs sans repasser par la mémoire DRAM globale et sans barrière côté hôte.
- **Bilan** :
  - *Forces* : Trafic DRAM minimal (tout reste dans les caches L2/LDS), 0 barrière CPU/GPU.
  - *Faiblesses* : Nécessite des atomiques globaux fiables ; l'upsampling nécessite une passe secondaire.

### 3.3 Méthode 3 : Spectral FFT Convolution Bloom (Fourier)

- **Origine & Adoption** : Unreal Engine 5 (*Physical Bloom Convolution*), CryEngine (*CineBloom*).
- **Fonctionnement** :
  - Transforme l'image et le kernel optique dans le domaine de Fourier : $\\mathcal{F}(I * K) = \\mathcal{F}(I) \\cdot \\mathcal{F}(K)$.
  - Multiplie point par point en complexe puis applique l'inverse IFFT.
- **Bilan** :
  - *Forces* : Simulation physique réelle des ouvertures d'objectif (bokeh polygonal, diffraction 6/8 branches, masques de salissure/flare). Coût invariant par rapport au rayon de flou.
  - *Faiblesses* : Coût fixe très lourd ($1.5 - 3.5\\text{ ms}$), inadapté aux moteurs visant $> 200\\text{ FPS}$.

### 3.4 Méthode 4 : Wave / Subgroup Intrinsics Bloom (Vulkan 1.3)

- **Origine & Adoption** : PlayStation 5, Xbox Series X, GPU RDNA2/3 et Nvidia RTX.
- **Fonctionnement** :
  - Utilise `subgroupQuadSwapHorizontal`, `subgroupQuadSwapVertical` et `subgroupShuffle`.
  - Les 4 invocations d'un Quad $2\\times2$ échangent directement leurs scalaires dans les registres ALUs SIMD de la wave (sans écriture LDS ou VRAM).
- **Bilan** :
  - *Forces* : Débit maximal, zéro empreinte LDS.
  - *Faiblesses* : Limité aux réductions $2\\times2$ basiques (complémentaire de l'approche pyramidale).

______________________________________________________________________

## 4. Matrice Comparative

| Critère | Dual-Filtering (Notre implémentation) | AMD SPD Single-Pass | Spectral FFT | Subgroup Intrinsics | Gaussian Legacy |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Dispatches Compute** | $2N - 1$ ($9\\text{ passes}$) | **1 dispatch** (down) | $3\\text{ dispatches}$ | $2N - 1$ | $2N\\text{ passes}$ (Ping-Pong) |
| **Bande Passante DRAM** | Faible (Demi-résolution) | **Minimale (LDS/L2)** | Élevée | **Quasi-Nulle** | Élevée |
| **Qualité & Anti-Flicker**| ⭐⭐⭐⭐⭐ (Karis 13-tap) | ⭐⭐⭐⭐ (Box/Custom) | ⭐⭐⭐⭐⭐ (Physique) | ⭐⭐⭐⭐ | ⭐⭐ (Boxy/Coupures) |
| **Complexité Code** | Faible / Modérée | Élevée (Atomiques) | Très Élevée | Modérée | Faible |
| **Effets Avancés (Bokeh)**| Non | Non | **Oui (Diffraction)**| Non | Non |
| **Temps GPU typique (1080p)** | **$0.20 - 0.79\\text{ ms}$** | **$0.10 - 0.30\\text{ ms}$**| $1.50 - 3.50\\text{ ms}$| $0.15 - 0.50\\text{ ms}$| $0.80 - 2.00\\text{ ms}$ |

______________________________________________________________________

## 5. Références Scientifiques, Conférences & Ressources Techniques

### 5.1 Dual-Filtering & Karis Weighting (Méthode 1)

- 📄 **Jorge Jimenez (Activision Blizzard / Call of Duty)** : [*Next Generation Post-Processing in Call of Duty: Advanced Warfare*](https://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare/), SIGGRAPH 2014 Advances in Real-Time Rendering in Games Course.
- 📄 **Brian Karis (Epic Games / Unreal Engine 4)** : [*High-Quality Post Processing in Unreal Engine 4 (SelfShadow Mirror)*](https://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf), SIGGRAPH 2013 Course on Real-Time Rendering.
- 📝 **Marius Bjørge (ARM / Frostbite)** : [*Bandwidth-Efficient Graphics with Pre-Calculated Textures & Dual-Filter Blur (SIGGRAPH Slides PDF)*](https://community.arm.com/cfs-file/__key/communityserver-blogs-components-weblogfiles/00-00-00-20-66/siggraph2015_2D00_mmg_2D00_marius_2D00_slides.pdf), ARM Research / ACM SIGGRAPH (2015).
- 🎓 **Joey de Vries (LearnOpenGL)** : [*Bloom Tutorial (Physically Based HDR Dual-Filter implementation)*](https://learnopengl.com/Advanced-Lighting/Bloom).

### 5.2 AMD Single-Pass Downsampler (SPD) (Méthode 2)

- 🏢 **AMD GPUOpen** : [*FidelityFX Single Pass Downsampler (SPD) Overview & Architecture*](https://gpuopen.com/fidelityfx-spd/).
- 💻 **AMD GPUOpen GitHub** : [*FidelityFX-SPD GLSL/HLSL Official Repository*](https://github.com/GPUOpen-Effects/FidelityFX-SPD).
- 💻 **AMD FidelityFX SDK** : [*FidelityFX-SDK Main Monorepo*](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK).

### 5.3 Spectral FFT Convolution Bloom (Méthode 3)

- 📄 **Tiago Sousa, Carsten Wenzel, Chris Raine (Crytek / CryEngine 3)** : [*Secrets of CryENGINE 3 Graphics (SIGGRAPH 2011 Courses)*](https://advances.realtimerendering.com/s2011/index.html), Real-Time Rendering Course.
- 📄 **Christian Schuler** : [*Fast Image Convolutions Using Discrete Fourier Transform on GPUs (GPU Pro / Springer)*](https://link.springer.com/chapter/10.1201/b10648-28).

### 5.4 Wave & Subgroup Intrinsics (Méthode 4)

- 📑 **Khronos Group Tutorial** : [*Vulkan Subgroup Tutorial: Cross-Invocation Communication with Quad and Shuffle*](https://www.khronos.org/blog/vulkan-subgroup-tutorial).
- 🎥 **Yury Uralsky (NVIDIA) & Matthäus G. Chajdas (AMD)** : [*Reading Between the Threads: High Performance Shader Programming with Vulkan Subgroups*](https://www.gdcvault.com/play/1026046/Reading-Between-the-Threads-High), GDC 2019 Vault.
- 📖 **Khronos Vulkan Registry** : [*Extension VK_EXT_subgroup_size_control Specification*](https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VK_EXT_subgroup_size_control.html).

### 5.5 Separable Gaussian Filtering (Méthode 5)

- 📝 **Daniel Rákos (RasterGrid)** : [*Efficient Gaussian Blur with Linear Sampling*](https://www.rastergrid.com/blog/2010/09/efficient-gaussian-blur-with-linear-sampling/), RasterGrid Tech Articles.
- 📄 **Filip Strugar (Intel)** : [*An Investigation of Fast Real-Time GPU-Based Image Blur Algorithms*](https://www.intel.com/content/www/us/en/developer/articles/technical/an-investigation-of-fast-real-time-gpu-based-image-blur-algorithms.html), Intel Developer Zone.
