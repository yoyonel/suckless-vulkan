# Rapport des Baselines de Référence : Profiling & Gestion Mémoire

Ce document consigne les baselines de référence mesurées sur `suckless-vulkan` (branche `feature/render-graph-integration`) avec les outils Heaptrack, Intel VTune Profiler et Valgrind Callgrind.

______________________________________________________________________

## 1. Synthèse Globale des Baselines

| Domaine d'Analyse | Outil Utilisé | Métrique Clé Mesurée | Valeur de Référence |
|---|---|---|---|
| **Allocations Heap** | Heaptrack | Peak Heap Consumption | **39.65 MB** |
| | | Allocations par seconde | ~65,300 allocs/s |
| | | Fuites mémoire résiduelles | 63.32 KB |
| **Accès Mémoire & Caches** | Intel VTune Memory Access | Memory Bound (P-Core) | **19.4%** des slots de pipeline |
| | | L1 Bound | **8.8%** des cycles |
| | | L2 Bound | **4.4%** des cycles |
| | | L3 Bound | **3.6%** des cycles |
| | | DRAM Bound | **13.1%** des cycles |
| | | LLC Miss Count | **2,450,834** |
| | | Average Memory Latency | **21 cycles CPU** |
| **Hotspots CPU** | Intel VTune Hotspots | Top CPU Function | Vulkan Driver (`libvulkan_intel.so`) & GLFW Event Poll |
| | | Temps CPU pur mesuré | 10.11s sur 24s d'exécution |
| **Instructions CPU** | Valgrind Callgrind | Program Total Instructions | ~244,404,459 Ir |
| **Concurrence & Threads** | Intel VTune Threading | Thread Oversubscription | 0.0% (Aucune contention excessive) |

______________________________________________________________________

## 2. Analyse Détaillée des Allocations Mémoire (Heaptrack)

### Métriques Mesurées

- **Peak Heap Memory** : 39.65 MB
- **Calls to Allocation Functions** : 935,502 appels

### Hotspots d'Allocations Identifiés

1. **`vk_draw_frame_internal` (\[`src/vk_engine_frame.cpp:245`\](../src/vk_engine_frame.cpp#L245))** :
   - **161,352 allocations** temporaires de `std::vector<rhi::PassDependency>` réallouées à chaque frame via `std::initializer_list`.
   - *Opportunité d'optimisation* : Remplacer par un tableau statique ou `std::span` pour atteindre 0 allocation par frame lors du rendu.
1. **Chargement de textures KTX / IBL** :
   - Allocations ponctuelles initiales pour le décodage et les buffers de staging (libérées proprement).

______________________________________________________________________

## 3. Analyse Matérielle des Caches CPU & DRAM (Intel VTune Memory Access)

### Répartition des Bloquages Mémoire (Pipeline Slots)

```text
Performance-core (P-core):
    Memory Bound: 19.4% of Pipeline Slots
        L1 Bound:   8.8% of Clockticks
        L2 Bound:   4.4% of Clockticks
        L3 Bound:   3.6% of Clockticks
        DRAM Bound: 13.1% of Clockticks
        Store Bound: 8.2% of Clockticks

Loads:        988,229,646
Stores:       554,216,626
LLC Misses:   2,450,834
Avg Latency:  21 cycles
```

______________________________________________________________________

## 4. Analyse des Hotspots CPU (Intel VTune Hotspots)

```text
Top Hotspots:
1. libvulkan_intel.so (Vulkan Command Buffer Submission & Driver State) : ~27.8%
2. libglfw.so.3 (X11 Event Polling & Window Management)                 : ~8.2%
3. libc.so.6 (System Calls & Synchronization)                          : ~6.7%
```

______________________________________________________________________

## 5. Analyse du Threading & Locks (Intel VTune Threading)

- **Total Thread Count** : 52 threads créés (Driver Mesa Intel, Workers Vulkan, Async Loader KTX, Tracy client).
- **Contention Mutex** : 0% de Spin / Overhead time.
- **Asynchronisme Validé** : Le worker asynchrone pour le chargement des cartes d'environnement HDR s'exécute sans bloquer le Main Thread de rendu.
