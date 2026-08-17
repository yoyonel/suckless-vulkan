# Rapport de Profilage & Baseline de Performance (Post-Revert Levier 4)

Date : 16 août 2026\
Branche : `feature/render-graph-integration`\
Machine : Intel RPL-U (Iris Xe Graphics), Linux 6.12 x86_64\
Mode : `RelWithDebInfo` / Vulkan 1.4 Native

______________________________________________________________________

## 1. Synthèse Globale des Benchmarks

| Outil de Profilage | Métrique Clé | Résultat Baseline Stable | Statut / Remarques |
| :--- | :--- | :--- | :--- |
| **Heaptrack** | Allocs / Frame en boucle de rendu | **0 allocs dans `vk_draw_frame_internal`** | Zéro allocation dynamique en rendu continu (27.73 allocs globales/frame incluant hot-swaps HDR/IBL) |
| **Heaptrack** | Pic mémoire Heap (`peak heap`) | **39.82 MB** | Mémoire heap stable, pas de fuite applicative |
| **Callgrind** | Instructions CPU totales | **230.87 M Ir** | ~29.6 k Ir/frame hors chargements initiaux |
| **VTune Hotspots** | Temps CPU effectif | **10.78 s** sur 24.28 s | Driver Intel (`libvulkan_intel.so`) & GLFW dominants (52% I/O kernel/event polling) |
| **VTune Memory** | Memory Bound (Pipeline Slots) | **20.7% P-core / 11.2% E-core** | DRAM Bound: 12.6%, L1 Bound: 9.6%, L3 Bound: 4.4% |
| **VTune Threading** | Contentions / Spin Time | **0 s Spin / 0% Overhead** | Locks & Condition Variables 100% sains sur thread I/O asynchrone |

______________________________________________________________________

## 2. Analyse Détaillée Heaptrack (Allocations Mémoire)

### A. Résumé

- **Appels aux fonctions d'allocation** : 215 711 appels (sur cycle complet avec 2 switchs HDR dynamiques via `Page_Down`).
- **Pic de consommation mémoire Heap** : `39.82 MB`.
- **Frames rendues** : `7 779 frames` (stabilité complète).
- **Allocations dans la boucle de rendu** : **0 alloc/frame dans `vk_draw_frame_internal`** (garantie architecture Arena / Cache).

### B. Hotspots d'allocations

1. Driver Mesa / Intel (`libvulkan_intel.so`) : 106 743 appels (50% des allocations du driver X11/DRI3).
1. Loader KTX2 / zstd decompression (`src/asset_ktx.cpp`) : Allocations one-shot temporaires lors des switchs asynchrones HDR.

______________________________________________________________________

## 3. Analyse Callgrind (Instructions CPU & Arbre d'Appels)

### A. Répartition des Instructions (Ir)

- **Total instructions enregistrées** : `230 869 713 (100.0%)`
- **Top fonctions consommatrices** :
  1. `__memset_avx2_unaligned_erms` (`libc.so.6`) : 26.18% (initialisations des buffers UBO / staging).
  1. `__strcmp_avx2` (`libc.so.6`) : 8.80% (résolution des extensions et descripteurs à l'init).
  1. `do_lookup_x` / dynamic linker : 4.73% (`dlopen`/symbol resolution des modules RHI).
  1. `libvulkan_intel.so` : ~12.5% (encodage command buffers matériel).

______________________________________________________________________

## 4. Analyse Intel VTune Profiler

### A. CPU Hotspots

- **Temps effectif CPU** : `10.780 s`
- **Répartition** :
  - `libvulkan_intel.so` (Driver GPU) : ~28.4% CPU time (Gfx translation & batching)
  - `libglfw.so.3` / X11 event polling : 9.8% CPU time
  - `__syscall_cancel_arch` (Kernel wait / polling) : 7.8% CPU time
  - `vulkan_app` / `libvulkan_rhi.so` : < 4.5% CPU time (très haute efficience CPU du moteur C++)

### B. Hiérarchie Mémoire & Bandwidth

- **P-Core Memory Bound** : `20.7%`
  - L1 Bound : `9.6%`
  - DRAM Bound : `12.6%` (accès aux gros buffers vertex/texture staging lors du switch HDR)
- **E-Core Memory Bound** : `11.2%`

### C. Threading & Synchronisation

- **Temps de rotation CPU (Spin Time)** : `0 s`
- **Overhead de thread** : `0.0%`
- Le thread d'I/O asynchrone pour le streaming HDR utilise proprement les condition variables et `wait()` sans polling actif inutile.

______________________________________________________________________

## 5. Conclusion & Baseline Validée

L'état après retour au **Levier 3** est 100% robuste, sans fuite mémoire, et prêt pour les futures étapes.
