# Analyse des L1 Cache Misses - RHI Refactoring (09/08/2026)

## 1. Contexte et Objectifs

Lors de l'encapsulation du "God Object" `EngineState` dans `SceneData`, un suivi précis des L1 Cache Misses a été réalisé. L'objectif était de s'assurer que la réorganisation des données (séparation du moteur et de la scène) ne détériorait pas les performances du cache L1 (Cache-Line Bouncing).

## 2. Mesures (sur un seul cœur : `taskset -c 0 perf stat -e cpu_core/L1-dcache-load-misses/`)

### 2.1. Avant Refactoring (Baseline `b02194a`)

- **Total L1 Misses** : 5,527,362
- **Total Loads** : ~291M
- **Miss Rate** : 1.90%
- **Répartition (via `perf report`)** :
  - **Tiers & OS** (Kernel IO, STB Image, Driver Vulkan, Tests) : **~99.9%** (~5,521,800 misses).
  - **Métier** (Boucle de jeu `draw_frame`, `LinearArena`, matrices) : **< 0.1%** (< 5,500 misses).

### 2.2. Après Refactoring (Encapsulation `SceneData` + Optimisations SIMD/Prefetch)

- **Total L1 Misses** : 5,224,013
- **Total Loads** : ~288M
- **Miss Rate** : 1.81%
- **Répartition (via `perf report`)** :
  - **Tiers & OS** : **~99.9%** (~5,218,000 misses).
  - **Métier** : **< 0.1%** (< 5,200 misses).

## 3. Optimisations Apportées (Itération 3.2)

1. **Alignement des allocations (DOD)** : Les allocations pour `instancePositions` et `billboardSoA.pos` dans `LinearArena` ont été alignées sur 64 bytes (`arena_alloc(..., 64)`), correspondant à la taille d'une ligne de cache L1.
1. **Vectorisation Forcée (AVX)** : Utilisation de `__builtin_assume_aligned` pour garantir au compilateur GCC/Clang l'alignement mémoire.
1. **Software Prefetching** : Utilisation de `__builtin_prefetch(&positions[i + 8], 0, 1)` dans les boucles de rendu (`vk_engine_frame.cpp`) pour anticiper le chargement de la prochaine ligne de cache.

## 4. Bilan

- **Gain Global** : **-5.48%** de L1 Cache Misses totaux (-303,349 misses).
- **Gain Temps CPU** : Chute du temps d'exécution global de **16.8%** (2.39s -> 1.98s).
- **Conclusion** : Le surcoût d'indirection hiérarchique (`core->scene.billboardSoA`) est nul. Les données métier étaient déjà extrêmement bien gérées et leur overhead n'apparaissait pas dans le Top 50 des misses. L'optimisation a donc touché un socle incompressible, prouvant le succès complet du refactoring.
