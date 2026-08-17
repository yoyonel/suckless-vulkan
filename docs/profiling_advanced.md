# Guide de Profiling Avancé : Heaptrack, Intel VTune & Callgrind

Ce document décrit l'utilisation des outils de profiling mémoire, CPU et threading intégrés dans `suckless-vulkan` pour établir des baselines de performances de référence.

______________________________________________________________________

## 1. Vue d'Ensemble des Outils

| Outil | Type d'Analyse | Métriques Clés | Commandes Justfile | GUI Associé |
|---|---|---|---|---|
| **Heaptrack** | Mémoire Heap & Allocations | Pic mémoire, nombre d'appels `malloc`/`free`, détection d'allocations dans la boucle de rendu | `just profile-heaptrack` | `just profile-heaptrack-gui` |
| **Intel VTune** (Memory) | Accès Mémoire & Caches | **L1/L2/L3 Cache Misses**, LLC Misses, bande passante DRAM, latence d'accès | `just profile-vtune-memory` | `just profile-vtune-gui` |
| **Intel VTune** (Hotspots) | CPU Pur & Assembler | Top 15 des fonctions C++ les plus consommatrices, temps CPU par ligne de code | `just profile-vtune-hotspots` | `just profile-vtune-gui` |
| **Intel VTune** (Threading) | Concurrence & Verrous | Contentions de mutex, temps d'attente (Wait Time), inactivité CPU | `just profile-vtune-threading` | `just profile-vtune-gui` |
| **Valgrind Callgrind** | Instruction-Level Call Graph | Compteurs d'instructions CPU exacts, graphe d'appel hiérarchique | `just profile-callgrind` | `kcachegrind` |

______________________________________________________________________

## 2. Compilation Pré-Requise

Tous les outils de profiling nécessitent un build optimisé conservant la table des symboles et les numéros de ligne (`-O3 -g`) :

```bash
just build-relwithdebinfo
```

______________________________________________________________________

## 3. Profiling des Allocations Mémoire (Heaptrack)

### A. Exécution CLI

```bash
just profile-heaptrack
```

Le script `scripts/benchmark_heaptrack.sh` exécute l'application de manière automatisée, enregistre les allocations et affiche un résumé direct :

- **Peak heap memory consumption** (pic d'utilisation du tas).
- **Calls to allocation functions** (nombre total d'allocations).
- **Top 5 hotspots d'allocation** avec stacktraces.

### B. Exploration Graphique

```bash
just profile-heaptrack-gui
```

Permet de visualiser :

- La courbe d'allocation mémoire au fil du temps.
- Les allocations temporaires créées et détruites rapidement.
- Les flammes d'allocation (Flamegraph).

______________________________________________________________________

## 4. Profiling Matériel Intel VTune

> [!NOTE]
> Intel VTune accède aux compteurs matériels du processeur (PMU) et requiert les permissions `sudo`. Les scripts gèrent automatiquement l'ajustement des permissions du répertoire de résultat pour l'utilisateur courant.

### A. Analyse des Accès Mémoire & Cache Misses (L1/L2/L3)

```bash
just profile-vtune-memory
```

Analyse l'efficacité des transferts mémoire CPU $\\leftrightarrow$ Mémoire / Caches :

- **L1/L2/L3 Cache Misses** : Mesure l'efficacité de la mise en cache des structures UBO, matrices GLM et buffers de transformation d'instances SSBO.
- **DRAM Bound** : Pourcentage de cycles d'exécution bloqués en attente de données en RAM.
- **Average Latency** : Latence moyenne des accès mémoire en cycles CPU.

### B. Analyse des Hotspots CPU

```bash
just profile-vtune-hotspots
```

Identifie les 15 fonctions les plus coûteuses en temps CPU avec demangling C++ automatique (`c++filt`).

### C. Analyse de Threading & Verrous

```bash
just profile-vtune-threading
```

Mesure l'utilisation des threads (Thread Concurrency), l'efficacité du worker de chargement I/O asynchrone et les contentions éventuelles.

### D. Exploration Graphique VTune

```bash
just profile-vtune-gui
```

Ouvre l'interface `vtune-gui` sur la dernière capture générée pour inspecter le code source C++ annoté et l'assembleur.

______________________________________________________________________

## 5. Profiling d'Instructions & Call Graph (Callgrind)

```bash
just profile-callgrind
```

Génère un fichier de trace dans `build/profiling/callgrind/callgrind.out` et affiche le résumé textuel via `callgrind_annotate`.

Pour explorer le graphe d'appel :

```bash
kcachegrind build/profiling/callgrind/callgrind.out
```

______________________________________________________________________

## 6. Sweep Global de Profiling

Pour exécuter une session complète de profilage (Heaptrack + VTune Hotspots + VTune Memory) en une seule commande :

```bash
just profile-all
```
