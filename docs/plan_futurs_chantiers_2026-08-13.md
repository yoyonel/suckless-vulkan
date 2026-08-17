# Vision & Roadmap des Chantiers Futurs

**Date** : 2026-08-13
**Contexte** : Suite à la clôture de la campagne d'optimisation mémoire (Zero-Allocation CPU et SSBO SoA), ce document explore trois pistes majeures pour faire passer le moteur au stade supérieur. Le "GPU-Driven Rendering" a été écarté du fait de la nature intrinsèque de la scène actuelle (une grille simple de 10x10 quads procéduraux/raytracés ne justifiant pas l'overhead d'une architecture Indirect Culling).

______________________________________________________________________

## 1. 🏗️ Architecture : Le Render Graph (Frame Graph)

### Description & Intérêt (Render Graph)

Un Render Graph (ou Frame Graph) est une abstraction de haut niveau permettant de déclarer dynamiquement les passes de rendu (Ombres, IBL, Opaque, Post-Process) et leurs dépendances. Au lieu de coder les `VkRenderPass` et les barrières en dur, le moteur "compile" le graphe et déduit automatiquement l'ordre d'exécution, les transitions d'images et la synchronisation optimale.

### Difficultés Techniques & Intégration (Render Graph)

- **Changement de Paradigme** : Remplacer l'approche monolithique actuelle par une exécution différée (Déclaration -> Compilation du Graphe -> Exécution).
- **Synchronisation Vulkan** : Le graphe doit être capable de générer des `VkImageMemoryBarrier` et `VkPipelineStageFlags` chirurgicales sans sur-synchroniser (ce qui tuerait le parallélisme GPU).
- **Dynamic Rendering** : Idéalement, cela devrait s'appuyer sur l'extension `VK_KHR_dynamic_rendering` pour s'affranchir de la rigidité des objets `VkFramebuffer` et `VkRenderPass` traditionnels.

### Gains Espérés (Render Graph)

- **VRAM Optimisée (Transient Aliasing)** : Le graphe sait quand une ressource temporaire (ex: Depth Buffer) n'est plus utilisée. Il peut allouer une même plage mémoire physique à une autre ressource (ex: G-Buffer) plus tard dans la frame.
- **Pipeline Extensible** : Ajouter une passe de post-processing devient trivial (quelques lignes de configuration) sans casser l'architecture.

### Risques Encourus (Render Graph)

- **Overhead CPU** : Si la compilation du graphe (évaluation des noeuds et dépendances) est exécutée à *chaque frame*, cela ruinera notre `L1 Cache Bound`.

### Validation & Benchmarks (Render Graph)

- **Profilage CPU (`perf stat`)** : Mesurer le temps d'exécution du "Graph Compiler". Il doit être nul en runtime (le graphe doit être compilé uniquement à l'initialisation ou au redimensionnement de la fenêtre).
- **Vulkan Validation Layers** : Zéro avertissement de synchronisation (Hazards) ou de layout d'image.

______________________________________________________________________

## 2. 🎨 Visuels : Finalisation du Color Pipeline (Post-Processing)

### Description & Intérêt (Color Pipeline)

Atteindre l'ISO-fonctionnalité visuelle avec le projet mère (`suckless-ogl`). Actuellement, seul le ToneMapping (Unreal) et le passage sRGB sont fonctionnels. L'objectif est d'implémenter une chaîne modulaire : Bloom (Mip-mapping), Aberration Chromatique, Color Grading, et Vignette.

### Difficultés Techniques & Intégration (Color Pipeline)

- **Compute vs Graphics** : Concevoir ces passes en Compute Shaders offre une meilleure utilisation de la bande passante (utilisation de la `Shared Memory` des Workgroups pour les filtres flous comme le Bloom).
- **Downsampling (Bloom)** : Implémenter un filtrage Dual-Filter (Kawase) efficace pour la chaîne de MipMaps sans exploser les reads/writes VRAM.
- **Intégration** : Ce chantier dépend fortement de la réussite du chantier **Render Graph** pour chainer les passes proprement.

### Gains Espérés (Color Pipeline)

- Qualité visuelle "Film Look" photoréaliste.
- Éclairage IBL sublimé par la dispersion (Bloom) des hautes intensités HDR.

### Risques Encourus (Color Pipeline)

- **Saturation VRAM / Bande passante** : Si les passes intermédiaires lisent/écrivent des images non-compressées `RGBA16F` en boucle vers la VRAM globale.

### Validation & Benchmarks (Color Pipeline)

- **Timestamps GPU** : Utiliser des `VkQueryPool` (Timestamps) pour encadrer chaque passe de post-process et mesurer son coût réel en millisecondes sur le GPU.
- **Nsight Graphics / RenderDoc** : Contrôler le `Cache Hit Rate L1/L2` côté GPU. Les passes de flou doivent prouver une utilisation optimale de la mémoire partagée et non pas saturer le DRAM Bandwidth.

______________________________________________________________________

## 3. 🛠️ Outillage : Fixer l'intégration Tracy Profiler

### Description & Intérêt (Tracy Profiler)

Le profiling est actuellement aveugle sur le croisement CPU/GPU en raison d'un bug multi-contexte avec Tracy. L'objectif est d'avoir une Timeline unifiée où l'envoi des commandes CPU s'aligne visuellement avec l'exécution réelle du GPU.

### Difficultés Techniques & Intégration (Tracy Profiler)

- **Calibration Horloges** : Synchroniser le timer CPU (QPC/TSC) avec le domaine temporel du GPU Vulkan (`vkGetCalibratedTimestampsEXT` ou gestion fine des dérives).
- **Gestion des Pools** : Gérer dynamiquement l'allocation des `VkQueryPool` par frame-in-flight sans faire exploser l'overhead CPU de `vkCmdWriteTimestamp`.

### Gains Espérés (Tracy Profiler)

- **Visibilité Chirurgicale** : Identifier immédiatement si le moteur est bloqué par la VSync, par l'attente d'une barrière (Pipeline Stall), ou si le post-processing s'exécute en parallèle du rendu géométrique. C'est l'outil indispensable pour valider les gains du *Render Graph*.

### Risques Encourus (Tracy Profiler)

- **Deadlocks** : Ajouter des hooks de profiling dans des files d'attente asynchrones (comme la file IBL existante) peut causer des interblocages.
- **Effet d'Observateur** : L'instrumentation Tracy pourrait introduire de l'overhead, modifiant le comportement même qu'elle est censée mesurer.

### Validation & Benchmarks (Tracy Profiler)

- **Overhead Test** : Comparaison A/B de l'application avec et sans `TRACY_ENABLE`. Le framerate, le `Memory Bound`, et le `L1 Bound` (mesurés via VTune/perf) ne doivent présenter aucune variation significative (> 1-2%).
- **Cohérence** : Les zones GPU rapportées par Tracy doivent correspondre à la milliseconde près aux temps rapportés par RenderDoc.
