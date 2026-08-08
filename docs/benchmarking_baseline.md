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

## Résultats Baseline (Avant Refactorisation SoA)

Ces résultats servent de point de référence absolu pour mesurer l'impact de nos futures optimisations Data-Oriented. Les tests durent ~15s avec deux échanges de HDR dynamiques.

### 1. Métriques CPU Cache (`perf stat`)

- **L1-dcache-load-misses :** ~35 312 669 (1.65% miss rate)
- **L1-dcache-loads :** ~2 140 671 158

*Analyse :* Le taux de miss cache (1.65%, soit >35 millions de défauts de cache) est élevé. Cela est typique d'une approche Array-of-Structures (AoS) comme l'utilisation d'un `std::vector<StructMassive>` dans la boucle de rendu et pendant le dispatch IBL.

### 2. Métriques Mémoire Heap (`heaptrack`)

- **Peak heap memory consumption :** 362.49K
- **Calls to allocation functions :** 2396 allocations (soit ~10239 allocations par seconde pendant le pic)
- **Total memory leaked :** ~255.82K (la majorité provenant des drivers X11/Mesa au démarrage)

*Analyse :* L'empreinte mémoire du moteur lui-même est extrêmement basse, ce qui respecte la philosophie "Handmade". Il n'y a pas d'allocations continues monstrueuses (comme recréer de gros `std::vector` à chaque frame), les appels se concentrent principalement lors des chargements de textures HDR. Cependant, on visera à annuler totalement ce chiffre grâce à un Custom Allocator (Linear Arena) pour l'état d'affichage dynamique.

## Prochaines Étapes

La baseline démontre que l'urgence n'est pas sur le volume de RAM consommé, mais sur **la linéarité en mémoire** pour le cache CPU.

L'Itération 3 consistera à remplacer l'AoS (Vector of BillboardInstance) par un **SoA (Structure of Arrays)** adossé à un **Arena Allocator** pour éliminer les ultimes allocations dynamiques et diviser drastiquement les L1 cache misses.
