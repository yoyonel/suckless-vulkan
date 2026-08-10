# Options d'Optimisation VRAM pour les Envmaps HDR (2026-08-09)

Ce document archive les pistes étudiées pour aller plus loin dans l'optimisation mémoire et temps de chargement des ressources HDR. Ces options interviennent après la validation de la **Phase 3 - Option C (Zero-Copy Staging Buffer)**.

## Option A : Compression GPU Native (BC6H)

**Statut :** Intéressant mais post-posé (Non Prioritaire).

### Concept

L'objectif est de réduire massivement la consommation VRAM des skybox (HDR RGBA32F = 16 octets/pixel -> BC6H = 1 octet/pixel). Une map 4K de 134 Mo tombe à ~22 Mo.

### Stratégie (Caveman Mode)

Plutôt que d'écrire un compresseur BC6H from scratch (très complexe sur CPU), l'idée est de :

1. Dégager le code de baking `.ktx2` manuel.
1. Appeler l'outil officiel Khronos `toktx` dans le système lors du cache miss (Slow Path) : `system("toktx --target_type BC6H --t2 <out> <in>")`.
1. Modifier le moteur pour lire le `vkFormat` du KTX2 et allouer une `VkImage` compressée (`VK_FORMAT_BC6H_UFLOAT_BLOCK`).

### Avantages espérés

- Division de la VRAM par 6 à 8.
- Bande passante mémoire ultra-réduite lors des `vk_ibl_bake` Compute Shaders.

______________________________________________________________________

## Option B : Pre-Baking IBL (Irradiance / Prefilter) hors-ligne

**Statut :** Rejeté (Anti-Pattern vis-à-vis des objectifs du moteur).

### Concept Initial

Exécuter les Compute Shaders `vk_ibl_bake` une seule fois, faire un readback VRAM -> RAM, puis stocker le résultat sur le disque (dans le `.ktx2` ou à côté). Le but était d'éviter les temps de calculs GPU (hitches) au chargement de l'environnement.

### Raison du Rejet

Le moteur a été conçu pour faire **le calcul IBL sur GPU au runtime**. C'est une vitrine technologique des capacités dynamiques (Compute Shaders) du moteur. Court-circuiter ce comportement en pré-calculant les ressources sur disque va à l'encontre même de l'objectif fondamental du projet (avoir un système d'éclairage dynamique réagissant au runtime). Le caching disque des Compute Shaders est donc hors-sujet ici.
