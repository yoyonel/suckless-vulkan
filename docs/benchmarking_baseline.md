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
