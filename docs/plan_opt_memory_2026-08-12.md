# Plan d'Optimisation Mémoire (Cible : Memory Bound ~15%)

**Date** : 2026-08-12

## 1. Analyse VTune & État des Lieux

- **Memory Bound Actuel** : ~21.4% (Pipeline Slots Stalled).
- **L1 Bound** : 12.4% -> Indique des évictions massives du cache L1 (micro-allocations dynamiques sur le chemin critique de rendu).
- **DRAM Bound** : 10.9% avec **2.6M LLC Misses** -> Indique de gros transferts RAM/Cache L3. Causé par la fragmentation du Tas (Heap) et la réallocation permanente de gros buffers (IBL, logs).

## 2. Objectif

- Réduire le Memory Bound à **15% (±1%)**.
- Zéro allocation dynamique (Heap) par frame sur le thread de rendu.

## 3. Plan d'Exécution (Chantier 3 : Allocations STL)

### Bloc 1 : Hot Path RHI (`vulkan_rhi.cpp`) - Cible L1 Bound

**Problème** : Création de `std::vector` à la volée (ex: bindings, barriers) à chaque drawcall ou update de descripteurs.
**Solutions** :

- Remplacer les `std::vector` par des `std::array` de taille fixe pré-définie (ex: `std::array<VkWriteDescriptorSet, 16>`).
- Utiliser `alloca()` pour allouer directement sur la Stack (pile) plutôt que sur le Tas (Heap).
- **KPI** : L1 Bound sous les 7%, stabilité parfaite des frametimes.

### Bloc 2 : IBL & Logs (`vk_engine_ibl.cpp`, `app_log.cpp`) - Cible DRAM Bound

**Problème** : `floatData` et `finalData` allouent des centaines de Mo pendant le pre-processing IBL. La file de logs réalloue des strings.
**Solutions** :

- **IBL** : Pré-allouer la mémoire via `std::vector::reserve()` dans une structure persistante pour réutiliser la mémoire entre les frames ou les passes de rendu.
- **Logs** : Utiliser un ring buffer pré-alloué de taille fixe pour le formatage des strings, ou utiliser un allocateur de pool (`std::pmr::monotonic_buffer_resource`).
- **KPI** : DRAM Bound sous les 8%, LLC Misses sous les 1.5M.

## 4. Protocole de Validation

- Exécuter `just perf-benchmark` après le Bloc 1 pour mesurer le L1-dcache-load-misses.
- Exécuter `just benchmark-vtune` (en root) après le Bloc 2 pour mesurer le Memory Bound global et la baisse des requêtes L3 -> RAM.
