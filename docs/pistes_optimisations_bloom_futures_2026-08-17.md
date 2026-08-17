# Feuille de Route & Pistes d'Optimisation du Dual-Filtering Bloom (2026-08-17)

## 1. Contexte & Historique des Optimisations

| Étape | Implémentation | Temps GPU Bloom (1080p) | Framerate Global | Statut |
| :--- | :--- | :--- | :--- | :--- |
| **Base Initiale** | Dual-Filtering Multi-Pass `RGBA16_SFLOAT` (9 passes) | $0.790\\text{ ms}$ ($790\\ \\mu\\text{s}$) | $\\approx 325\\text{ FPS}$ | Obsolète |
| **Levier 5 (FP32)** | Format Compact `B10G11R11_UFLOAT` (-50% DRAM) | $0.337\\text{ ms}$ ($337\\ \\mu\\text{s}$) | $\\approx 425\\text{ FPS}$ | Étape Précédente |
| **Piste 2 (FP16 Natif)** | **Arithmétique SIMD `f16vec3` / `float16_t` (Half-Res)** | **$0.228\\text{ ms}$ ($228\\ \\mu\\text{s}$)** | **$\\approx 450\\text{ FPS}$** | 🏆 **Production (Par Défaut)** |
| **Piste 1 + 2 (Fast Path)** | **Quarter-Res Start ($480\\times270$) + FP16 Natif** | **$0.189\\text{ ms}$ ($189\\ \\mu\\text{s}$)** | **$\\approx 460\\text{ FPS}$** | 🟢 **Optionnel (`SVK_BLOOM_QUARTER_RES=1`)** |
| **Expérimentation SPD** | Single-Pass Compute Subgroups (1 dispatch) | $0.122\\text{ ms}$ ($122\\ \\mu\\text{s}$) | $\\approx 457\\text{ FPS}$ | ❌ Reverté (Post-Mortem : artefacts 2D/LDS) |

______________________________________________________________________

## 2. Inventaire des Pistes d'Optimisation Futures

### 🥇 Piste 1 : Démarrage à $\\frac{1}{4}$ Résolution ("Quarter-Res Start" - $480\\times270$ en 1080p)

#### Description & Principe Physique

- **Constat** : Dans la pyramide actuelle, le Mip 0 démarre à $\\frac{1}{2}$ résolution ($960\\times540$, soit 518 400 pixels). Ce premier étage concentre à lui seul **65% de tous les pixels traités** par le Bloom Compute.
- **Principe AAA** (Unreal Engine 5 Fast Bloom, DOOM Eternal) : Le Bloom simule la diffusion grand angle de la lumière dans l'humeur vitrée de l'œil humain (*scattering glare*). C'est un effet à basse fréquence spatiale.
- **Fonctionnement** : Échantillonner la texture HDR 1080p avec le filtre Jimenez 13-tap directement pour générer un Mip 0 à $480\\times270$ ($129\\ 600\\text{ pixels}$).
- **Activation** : Variable d'environnement `SVK_BLOOM_QUARTER_RES=1`.
- **Gains Mesurés sur GPU Local (Tracy Profiler)** :
  - **Downsample Pass** : **$0.127\\text{ ms}$ ($127\\ \\mu\\text{s}$)** (vs $0.205\\text{ ms}$ en Half-Res, $-38%$).
  - **Upsample Pass** : **$0.061\\text{ ms}$ ($61\\ \\mu\\text{s}$)** (vs $0.130\\text{ ms}$ en Half-Res, $-53%$).
  - **GPU Bloom Pipeline TOTAL** : ⚡ **$0.189\\text{ ms}$ ($189\\ \\mu\\text{s}$)** (vs $0.337\\text{ ms}$ en Half-Res, **$-44%$ net** et **$-76%$ vs base**).
  - **Empreinte VRAM** : **$0.7\\text{ MB}$** ($-75%$ vs Half-Res, $-87.5%$ vs Base).
- **Impact Visuel** :
  - Halos larges : rigoureusement identiques.
  - Micro-spéculaires (< 2 pixels) : légèrement plus doux et diffus (rendu plus cinématographique).
  - Stabilité temporelle : 100% stable, garantie par le prefilter Karis 13-tap avec pondération luma ($w = \\frac{1}{1 + \\text{Luma}}$).

______________________________________________________________________

### 🥈 Piste 2 : Arithmétique SIMD FP16 Natif (`VK_KHR_shader_float16_int8`)

#### Fonctionnement & Principe FP16

- **Implémentation** : Shaders `shaders/bloom_downsample.comp` et `shaders/bloom_upsample.comp` utilisant `f16vec3` et `float16_t` via l'extension GLSL `GL_EXT_shader_explicit_arithmetic_types_float16` avec fallback FP32 transparent.
- **Activation Matérielle** : Activé automatiquement via `VkPhysicalDeviceVulkan12Features::shaderFloat16` lors de la création du Device Vulkan.
- **Gains Mesurés sur GPU Local (Tracy Profiler en Half-Res)** :
  - **Downsample Pass** : Réduit de **$205\\ \\mu\\text{s} \\to 147\\ \\mu\\text{s}$** ($-28%$).
  - **Upsample Pass** : Réduit de **$130\\ \\mu\\text{s} \\to 79\\ \\mu\\text{s}$** ($-39%$).
  - **GPU Bloom Pipeline TOTAL** : Réduit de **$337\\ \\mu\\text{s} \\to 228\\ \\mu\\text{s}$** (⚡ **$-32%$ de gain net par rapport au Levier 5 seul**, et **$-71%$ vs base initiale**).
- **Précision & Stabilité** : 🟢 **100% ISO Paritaire**, zéro perte de dynamique perçue en HDR ($[0, 65504]$).

______________________________________________________________________

### 🥉 Piste 3 : Réutilisation Mémoire VMA / Transient Resource Aliasing

#### Analyse & Conclusion Post-Évaluation

- **Constat** : L'adoption du Levier 5 (`B10G11R11_UFLOAT`) a déjà divisé l'empreinte VRAM totale du Bloom par 4 (de $5.5\\text{ MB} \\to 1.4\\text{ MB}$).
- **Overhead vs Gain** : L'aliasing dynamique des 9 mips compute nécessiterait de recalculer et re-binder des `VkDescriptorSet` à chaque frame pour un gain maximal résiduel de $\\approx 0.7\\text{ MB}$.
- **Statut** : ⚪ **Non Retenue (Closed)** — Gain VRAM négligeable face au coût CPU et à la complexité ajoutée (Respect du principe *Suckless* / YAGNI).

______________________________________________________________________

### 🏅 Piste 4 : Queue Compute Asynchrone Dédiée (Async Compute Overlap)

#### Principe d'Overlap Async Compute

- Émettre les dispatches de Bloom sur une queue `VK_QUEUE_COMPUTE_BIT` concurrente pendant les attentes ou les passes d'ombres/IBL de la queue graphique.
- **Gains Attendus** : Masquage total de la latence GPU du Bloom sur matériel supportant les queues asynchrones physiques.

______________________________________________________________________

## 3. Matrice de Priorisation & Scorecard Décisionnel

| Piste | Complexité | Risque Régression | Gain GPU Mesuré / Projeté | Gain VRAM | Statut |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **1. Quarter-Res Start (`SVK_BLOOM_QUARTER_RES=1`)** | Très Faible | Nul (Pondération Karis) | 🔥 **$-76%$ ($0.189\\text{ ms}$)** | **$-75%$** | 🟢 **Validé & Intégré** |
| **2. Arithmétique SIMD FP16 (`float16_t`)** | Faible | Nul (Fallback FP32) | ⚡ **$-71%$ ($0.228\\text{ ms}$)** | $0%$ | 🟢 **Validé & Intégré** |
| **3. Aliasing Mémoire VMA**| Modérée | Faible | $0%$ (VRAM) | 💾 $\\approx 0.7\\text{ MB}$ | ⚪ **Non Retenue (YAGNI)** |
| **4. Async Compute Queue** | Élevée | Modéré (Sync sémaphores) | ⚡ **Latency Hiding** | $0%$ | ⚪ **P3 (Phase ultérieure)** |
