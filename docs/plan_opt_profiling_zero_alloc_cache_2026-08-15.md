# Plan d'Optimisation : Rendu Zéro-Allocation, Caches CPU & Batching Vulkan

- **Date de création** : 2026-08-15
- **Branche de référence** : `feature/render-graph-integration`
- **Objectif** : Éliminer les allocations dynamiques de la boucle de rendu, optimiser les accès caches L1/L2/L3 et réduire le temps passé dans le driver Vulkan.

______________________________________________________________________

## 1. État des Lieux & Baselines de Référence Initiales

Les mesures de référence suivantes ont été établies le 15 août 2026 via la suite d'outils automatisée :

| Domaine | Outil de Mesure | Métrique Clé | Baseline Initiale | Objectif Visé |
|---|---|---|---|---|
| **Allocations Heap** | `just profile-heaptrack` | Allocations par frame dans la boucle de rendu | **$161,352$ allocs** (`std::vector<PassDependency>`) | **$0$ alloc / frame** |
| | | Pic mémoire Heap | **$39.65\\text{ MB}$** | $\\le 39.0\\text{ MB}$ |
| **Caches CPU** | `just profile-vtune-memory` | **L1 Bound** (P-Core) | **$8.8%$** des cycles CPU | **$< 4.0%$** |
| | | **L2 Bound** (P-Core) | **$4.4%$** des cycles CPU | **$< 2.5%$** |
| | | **DRAM Bound** (Accès RAM) | **$13.1%$** des cycles CPU | **$< 7.0%$** |
| | | **LLC Miss Count** (Cache L3) | **$2,450,834$ misses** | **$< 1,200,000$** |
| | | **Latence moyenne mémoire** | **$21\\text{ cycles}$** | **$\\le 15\\text{ cycles}$** |
| **Hotspots CPU** | `just profile-vtune-hotspots` | Part Driver Vulkan (`libvulkan_intel.so`) | **$27.8%$** du temps CPU pur | **$< 15.0%$** |
| **Instructions CPU** | `just profile-callgrind` | Nombre d'instructions total (Ir) | **$244,404,459\\text{ Ir}$** | **$< 180\\text{M Ir}$** |

______________________________________________________________________

## 2. Phase 1 : Rendu Zéro-Allocation (Heap Elimination)

### 2.1 Problématique des Allocations par Frame

L'analyse Heaptrack a révélé que la méthode `vk_draw_frame_internal` (\[`src/vk_engine_frame.cpp:245`\](file:///home/latty/Prog/__PERSO__/suckless-vulkan/src/vk_engine_frame.cpp#L245)) et le module \[`src/rhi/render_graph.cpp`\](file:///home/latty/Prog/__PERSO__/suckless-vulkan/src/rhi/render_graph.cpp) instancient des `std::vector<rhi::PassDependency>` temporaires via `std::initializer_list` à chaque frame ($65,000\\text{ allocs/sec}$).

### 2.2 Modifications Architecturales Phase 1

1. Remplacer les passages de `std::vector<rhi::PassDependency>` par des `std::span<const rhi::PassDependency>`.
1. Utiliser des conteneurs statiques à taille fixe (`std::array<rhi::PassDependency, N>` ou petit buffer inline `SmallVector<rhi::PassDependency, 8>`).
1. Intégrer un allocateur linéaire dédié (`FrameArena`) pour les nœuds et vues éphémères du RenderGraph réinitialisé à chaque début de frame (`arena.reset()`).

### 2.3 Protocole de Test Phase 1

```bash
# 1. Tests unitaires et logiques
just test

# 2. Test d'intégration complet
just test-integration

# 3. Validation de non-régression mémoire sous AddressSanitizer / LeakSanitizer
just build-asan
just stress-fullscreen-asan 50 100
```

### 2.4 Protocole de Profiling & Mesure de Progression Phase 1

```bash
# Exécution du benchmark Heaptrack
just profile-heaptrack

# Vérification du résultat CLI
# La section "TOP 5 HOTSPOTS D'ALLOCATION" ne doit plus contenir vk_draw_frame_internal
just profile-heaptrack-gui
```

- **Critère de Succès Phase 1** : Nombre d'allocations par frame $= 0$ après initialisation.

______________________________________________________________________

## 3. Phase 2 : Optimisation Caches CPU L1/L2/L3 & Staging Buffers Contigus

### 3.1 Problématique des Caches CPU & DRAM

Intel VTune indique que $19.4%$ des slots d'exécution sont pénalisés par la mémoire ($8.8%$ en L1 et $13.1%$ en DRAM). La cause principale est la dispersion des structures de données uniformes et les copies répétées CPU $\\rightarrow$ Host-Visible Memory sans alignement sur la taille de ligne de cache ($64\\text{ octets}$).

### 3.2 Modifications Architecturales Phase 2

1. **Alignement strict sur les lignes de cache ($64\\text{B}$)** :
   - Application de `alignas(64)` sur les structures envoyées aux UBOs (`GlobalUniforms`, `ObjectUniforms`, `CameraUniforms`).
1. **Ring Buffer VMA Persistant** :
   - Remplacer les allocations/mappages répétés de staging buffers par un **Ring Buffer Host-Visible VMA persistant** mappé une seule fois à l'initialisation, éliminant les overheads de `vkMapMemory`/`vkUnmapMemory`.
1. **Data-Oriented Layout (SOA)** :
   - Regroupement contigu des données d'instances et matrices de modèle pour maximiser le préchargement matériel (Hardware Prefetcher).

### 3.3 Protocole de Test Phase 2

```bash
# 1. Vérification de la parité des calculs IBL et shaders
just verify-ibl

# 2. Test visuel automatisé dans le conteneur CI
just ci-docker Release
```

### 3.4 Protocole de Profiling & Mesure de Progression Phase 2

```bash
# Mesure de l'accès mémoire avec VTune
just profile-vtune-memory

# Inspection détaillée
just profile-vtune-gui
```

- **Critère de Succès Phase 2** :
  - **L1 Bound** $< 4.0%$.
  - **DRAM Bound** $< 7.0%$.
  - **LLC Misses** $< 1,200,000$.

______________________________________________________________________

## 4. Phase 3 : Coalescence des Barrières & Optimisation Command Buffer

### 4.1 Problématique du Driver Overhead

VTune CPU Hotspots indique que $27.8%$ du temps CPU est consommé dans `libvulkan_intel.so`, causé par des barrières de pipeline individuelles séquentielles et le ré-enregistrement redondant des passes statiques.

### 4.2 Modifications Architecturales Phase 3

1. **Coalescence des Barrières (`VkDependencyInfo` / `VkMemoryBarrier2`)** :
   - Regrouper toutes les transitions d'images et de buffers inter-passes dans un seul appel `vkCmdPipelineBarrier2` unifié en début de RenderPass.
1. **Command Buffer Reusability** :
   - Pré-enregistrer les passes invariantes (Skybox, debug grid) dans des command buffers secondaires pour éviter leur ré-encodage à chaque frame.

### 4.3 Protocole de Test Phase 3

```bash
# 1. Validation stricte avec Vulkan Validation Layers
just check-validation-layers

# 2. Scénario d'intégration et timeline Tracy
just test-integration-tracy
```

### 4.4 Protocole de Profiling & Mesure de Progression Phase 3

```bash
# 1. Mesure du temps passé dans le driver Vulkan
just profile-vtune-hotspots

# 2. Mesure de la réduction d'instructions CPU globales
just profile-callgrind
```

- **Critère de Succès Phase 3** :
  - Part CPU `libvulkan_intel.so` $< 15.0%$.
  - Nombre total d'instructions Callgrind $< 180\\text{M Ir}$.

______________________________________________________________________

## 5. Matrice Récapitulative & Tableau de Suivi Avant / Après

Ce tableau sera mis à jour à l'issue de chaque phase :

| Métrique / Outil | Baseline Initiale | Résultat Phase 1 | Résultat Phase 2 | Résultat Phase 3 | Cible Finale |
|---|---|---|---|---|---|
| **Allocs Heap / frame** (`heaptrack`) | $161,352$ | **$0$** (🎯 Atteint) | **$0$** (🎯 Maintenu) | **$0$** (🎯 Maintenu) | **$0$** |
| **Total Allocs** (`heaptrack`) | $935,502$ | **$247,204$** | **$247,204$** | **$211,365$** (🟢 $-77.4%$) | Minimiser |
| **Peak Heap** (`heaptrack`) | $39.65\\text{ MB}$ | **$39.65\\text{ MB}$** | **$39.65\\text{ MB}$** | **$39.65\\text{ MB}$** | **$\\le 39.0\\text{ MB}$** |
| **L1 Bound** (`vtune memory`) | $8.8%$ | *Phase 2* | **$10.7%$** | **$14.7%$** | **$< 4.0%$** |
| **DRAM Bound** (`vtune memory`) | $13.1%$ | *Phase 2* | **$12.1%$** | **$13.1%$** | **$< 7.0%$** |
| **LLC Misses** (`vtune memory`) | $2,450,834$ | *Phase 2* | **$1,950,819$** (🟢 $-20.4%$) | **$1,950,819$** (🟢 $-20.4%$) | **$< 1.2\\text{M}$** |
| **Latence Mémoire Moyenne** | $21\\text{ cycles}$ | *Phase 2* | **$16\\text{ cycles}$** | **$11\\text{ cycles}$** (🟢 $-47.6%$) | Minimiser |
| **Driver Vulkan CPU** (`vtune hotspots`) | $27.8%$ | **$26.0%$** | **$26.0%$** | **$24.7%$** (🟢 $-3.1%$) | **$< 15.0%$** |
| **Instructions CPU** (`callgrind`) | $244.4\\text{M Ir}$ | **$218.8\\text{M Ir}$** | **$218.8\\text{M Ir}$** | **$223.0\\text{M Ir}$** | **$< 180\\text{M Ir}$** |
| **Validation CI (8/8 jobs)** | ✅ Vert | ✅ **Vert (Pass)** | ✅ **Vert (Pass)** | ✅ **Vert (Pass)** | ✅ Vert |
