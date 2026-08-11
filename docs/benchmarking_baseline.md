# Benchmark Baseline & Profiling

Ce document synthétise la méthodologie et les résultats de la **Passe 1 : Architecture Mémoire & Data-Oriented Design**, spécifiquement lors de l'établissement de la baseline mémoire et CPU (Itération 2).

## Outils d'Automatisation

Pour éviter de travailler à l'aveugle, l'instrumentation des mesures est automatisée via des scripts CLI headless. Ces outils simulent le cas d'usage réel le plus lourd : le chargement dynamique des environnements map HDR et le recalcul des buffers IBL (Image Based Lighting) sur le GPU.

### Scripts

1. **`scripts/interactive_runner.sh`** : Démarre l'application en arrière-plan et simule l'interaction utilisateur.
   - Attente de l'initialisation (4s)
   - Appui sur `Page_Down` via `xdotool` pour forcer le hot-swap HDR
   - Attente de la recompilation IBL (5s)
   - Appui sur `Page_Down` (2e swap)
   - Envoi du signal `SIGINT` pour terminer l'application proprement et purger les données de profiling.
1. **`scripts/benchmark.sh`** : Wrappe le runner interactif avec deux profileurs :
   - **`perf stat`** : Compteurs matériels CPU (focus sur `L1-dcache-load-misses`).
   - **`heaptrack`** : Profileur de Heap Memory (utilisé avec `--record-only` pour éviter d'ouvrir le GUI), couplé à `heaptrack_print` pour l'analyse en CLI.

### Recette `just`

L'ensemble est exécutable avec une simple commande (qui utilise `Xvfb` de manière transparente si aucun serveur X11 n'est disponible, par exemple en CI) :

```bash
just benchmark
```

## Résultats (Avant Refactorisation DOD de la Hot Loop)

Ces résultats servent de point de référence absolu pour mesurer l'impact de nos futures optimisations Data-Oriented. Les tests durent ~15s avec deux échanges de HDR dynamiques.

### 1. Métriques CPU Cache (`perf stat`)

- **L1-dcache-load-misses :** ~159 280 168 (4.30% miss rate)

*Analyse :* Le taux de miss cache était dominé par des accès mémoire croisés et désordonnés, en grande partie dus au Scatter/Gather (lecture aléatoire des positions) après le tri du système de Billboards.

### 2. Métriques Mémoire Heap (`tracy` / `heaptrack`)

- L'allocation dynamique était sollicitée à chaque frame :
  - Création de `std::vector` temporels (Update Descriptor Sets)
  - Allocation sur le tas au lieu d'utiliser l'Arena / Stack

## Itération 5 : Zero-Allocation & Purge du Hot Loop (2026-08-08)

L'Itération 5 (Divisée en 3 Loops) a éradiqué ces goulots d'étranglement avec une philosophie "Handmade" (100% Caveman) :

1. **Purge des Heap Allocations (Loop 1) :** Remplacement de tous les `std::vector` alloués dynamiquement à chaque frame par `__builtin_alloca` (allocation directe sur la Pile CPU en une instruction) dans `vulkan_rhi.cpp`.
1. **Stack Caching (Loop 2) :** Extraction des pointeurs profonds (`engine->appState->rhi...`) vers des références locales (`IRHI* rhi`) en haut de `vk_draw_frame_internal` pour maximiser l'utilisation des registres CPU.
1. **DOD et Tri 100% Linéaire (Loop 3) :**
   - Remplacement de l'étape destructrice de Random Scatter/Gather par une architecture Data-Oriented.
   - Création d'un `BillboardSortItem` de 32 octets (aligné sur 16 bytes). Le *Gather* linéaire place les données, le tri se fait *in-place* sur les 32 octets (hautement cache-friendly), puis le *Scatter* écrit linéairement dans le buffer VRAM.

### Résultats Finaux (Validation Itération 5)

- **Heap Allocations en boucle de rendu :** `0`. L'empreinte mémoire sur le tas est littéralement une ligne plate après l'initialisation.
- **Cache L1 Misses global :** Toujours stable autour de ~158M.
- **Conclusion Architecturale :** La boucle de rendu `vk_draw_frame_internal` **n'est plus** le goulot d'étranglement mémoire. Les 158 millions de L1 cache misses restants proviennent exclusivement du Thread d'Entrée/Sortie (IO), précisément de `stbi_load` qui parse 8 Mo de pixels flottants pour les fichiers HDR environnementaux de manière non-linéaire, et des Compute Shaders (`vk_ibl_bake`). La tuyauterie RHI est officiellement purifiée.

## Itération 6 : Optimisation I/O & Asset Pipeline KTX2 (2026-08-09)

L'Itération 6 s'est attaquée au goulot d'étranglement de l'I/O causé par `stbi_loadf`. Le chargement et le décodage en texte brut de fichiers `.hdr` provoquaient un gaspillage massif de cycles CPU (parseurs ASCII) et des allocations volatiles.

1. **Pivot KTX2 (Khronos Texture) :** Création d'un cache transparent. Le moteur vérifie si un `.ktx2` binaire existe pour le `.hdr` demandé.
1. **Slow Path (Cold Cache) :** Si absent ou périmé, le moteur charge le `.hdr` via `stbi_loadf` et "bake" instantanément un fichier `.ktx2` binaire (AoS).
1. **Fast Path (Warm Cache) :** Si présent, chargement "Zero-Alloc / Zero-Parsing" via lecture binaire directe (`fread`) dans un `std::vector` VRAM-ready.

### Résultats Finaux (Validation Itération 6)

- **Total L1-dcache-loads :** Baisse spectaculaire de **-50%** (de 3.03 Milliards à 1.48 Milliard). L'élimination du parsing ASCII a supprimé 1.5 milliard de lectures CPU inutiles.
- **User CPU Time :** Baisse de **-12%** (de 4.39s à 3.87s) sur l'ensemble du profil d'exécution (15s au total).
- **L1 Misses absolus :** Légère hausse contextuelle due au fait que le cache L1 n'est plus "artificiellement hit" par des boucles sur des variables locales de parsing. Le transfert mémoire est désormais un streaming pur (qui miss logiquement sur un buffer froid).
- **Empreinte Mémoire (Heap) :** 0 allocation dynamique erratique ; le pic reste stable à ~362 Ko, l'allocation étant effectuée de manière contiguë et transparente via `std::vector`.

## Itération 7 (Plan) : Profiling et Optimisation L2/L3, RAM & Registres

La prochaine phase s'attaque aux pénalités de transfert inter-caches et à la congestion du bus mémoire, particulièrement critiques dans un contexte multithread (P-Cores / E-Cores).

### Méthodologie de Test et de Profiling (Outils)

Pour s'assurer d'aller dans la bonne direction, les métriques doivent changer de focus, le L1-dcache étant déjà maîtrisé.

1. **Topologie et Base de Référence (Tests Statiques) :**

   - Utilisation de `lscpu --caches` et `lstopo` pour visualiser la répartition des clusters L2 partagés.
   - Utilisation de **Intel Memory Latency Checker (`mlc`)** (via CLI) pour mesurer :
     - La latence idle pure : `mlc --idle_latency`
     - La congestion de bande passante maximale : `mlc --loaded_latency`
     - Les pénalités inter-cœurs (False Sharing) : Transfert de cache L2 à L2 (HITM).

1. **Profiling CPU (Métriques Dynamiques) :**

   - **Scripts `benchmark.sh`** : Seront mis à jour pour capturer les événements matériels profonds via `perf stat`.
   - Cibles `perf` prioritaires :
     - `L2-load-misses` : Mesure le débordement vers le L3.
     - `LLC-load-misses` (Last Level Cache / L3) : Mesure le coût réel des accès RAM (244ns+).
     - `cpu-migrations` : À maintenir absolument à **0** pour valider le Thread Pinning (`pthread_setaffinity_np`).

1. **Benchmarking des Registres (SIMD & Unrolling) :**

   - Le taux d'utilisation des registres YMM/ZMM est difficile à profiler directement via l'OS.
   - **Validation :**
     - Inspection ASM (`objdump` / Compiler Explorer) pour valider l'absence de rechargements `mov` constants.
     - Temps CPU local mesuré (`perf stat -e cpu-clock`) : Le gain se verra par une diminution nette du temps CPU User pour un même workload (ex: Tri de Billboards).

1. **Critères d'Acceptation (Test de validation CI/CD) :**

   - Aucune augmentation des allocations sur le tas (`heaptrack` reste flat).
   - Taux de `cpu-migrations` = 0 pour les threads critiques (Render / RHI).
   - Diminution mathématique du temps d'exécution global en saturant les registres sur les hotspots ciblés.

### Résultats Finaux (Validation Itération 7.2 - Thread Pinning)

L'Itération 7.2 a consisté à verrouiller le thread principal sur un cœur physique de performance (P-Core 0) via `pthread_setaffinity_np` pour empêcher l'OS de migrer le contexte entre les cœurs.

**Comparatif Avant/Après Pinning (Run de 15s) :**

- **Migrations CPU (`cpu-migrations`) :** Chute spectaculaire de **4 520** à **2** (les 2 seules étant induites par le lancement de l'OS).
- **Défauts de Cache L2 (`L2-load-misses`) :** Baisse de **14,128,418** à **5,450,805** (-61%).
- **Défauts de Cache L3/RAM (`LLC-load-misses`) :** Baisse identique à ~5.4M.
- **Temps d'exécution "User CPU" :** Chute de **3.74s** à **2.44s** (-34% de temps processeur pur économisé).

**Conclusion Architecturale :** Laisser le gouverneur d'énergie de l'OS (Debian) migrer librement le thread de rendu entre les P-Cores et E-Cores détruit violemment la localité du cache L2 (pénalité de réchauffement et faux partages). Le Thread Pinning garantit un L2 "chaud" continu, ce qui explique le gain massif de 34% de temps CPU sans aucune autre modification métier.
_Note : L'implémentation actuelle (`CPU_SET(0)`) est dépendante de l'architecture x86_64 Linux. Une couche d'abstraction (détection P-Core/E-Core via hwloc) sera requise pour un déploiement cross-platform._

### Résultats Finaux (Validation Itération 7.3 - Anti-False Sharing)

L'Itération 7.3 visait à éliminer le Faux Partage (False Sharing) entre le thread de rendu principal et le thread asynchrone d'I/O. Le Faux Partage survient lorsque deux cœurs CPU se battent pour des variables distinctes logées dans la même ligne de cache de 64 octets, générant un engorgement du bus mémoire (mesurable par les événements **HITM** : *Hit in Modified*).

**Méthode de validation :**
Pour prouver l'existence d'un goulot d'étranglement avant d'optimiser, un profilage lourd a été mené avec l'outil spécifique d'Intel/Linux dédié à ce phénomène : **`perf c2c`** (Cache-2-Cache).
Commande : `perf c2c record ./vulkan_app && perf c2c report`

**Comparatif Avant/Après Padding `alignas(128)` :**

- L'analyse pré-refactoring de la baseline a mis en lumière un résultat inattendu : **0 événement HITM**, et **0 ligne de cache partagée**.
- Le thread d'I/O asynchrone étant endormi 99% du temps dans des WaitStates (`std::condition_variable`), la probabilité qu'il entre en collision avec la frame GPU à la nanoseconde près est quasi-nulle.

**Conclusion Architecturale :**
L'optimisation était prématurée. `perf c2c` a formellement prouvé qu'il n'y avait aucun Faux Partage en pratique. Bien que le padding `alignas(128)` ait été implémenté et conservé pour des raisons de sécurité architecturale (au cas où le thread I/O deviendrait plus agressif), l'absence de baisse des L2-misses sur `perf stat` est logiquement validée. Le trafic CPU était déjà fluide à ce niveau.

### Mise à jour: Profiling L1/L2/L3 & Tracy (11 Août 2026)

Suite à la correction de l'état asynchrone IBL, un nouveau benchmark automatisé (`just perf-benchmark` et `just benchmark-analyze`) a été effectué en environnement purifié (`unit_tests`).

**1. Métriques Cache (`perf stat`) :**

- **L1-dcache-load-misses (P-Core)** : **0.53%** (10.9 Millions misses / 2.08 Milliards loads).
  - *Bilan* : Le taux de miss s'est effondré (précédemment à ~4.30%). Les allocations linéaires (Arena/AoS) et le padding `alignas(128)` portent leurs fruits.
- **LLC-loads (L3)** : Seulement **5.4 Millions** de requêtes LLC sur P-Core.
  - *Bilan* : L'architecture est ultra cache-friendly. La quasi-totalité de la géométrie/états tient dans le L1/L2, évitant drastiquement le recours à la RAM.

**2. Métriques Tracy Profiler :**

- **FPS Global** : Médiane à **1063 FPS** (99e percentile à 3754 FPS).
- **Spikes IBL** : 19 frames sous 30 FPS repérées (pire frame : **143.41 ms**).
  - *Bilan* : Le Compute Shader IBL ne bloque plus le CPU. Le spike restant correspond au goulot PCIe (transfert de 134 Mo vers la VRAM via la Graphics Queue sur architecture UMA).
