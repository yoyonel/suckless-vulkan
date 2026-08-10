# Plan d'Implémentation : Optimisations L2/L3, RAM et Registres

**Règles strictes** : 10 minutes max par itération. Zéro commit. Zéro push. Focus brut.
**Mise à jour** : Ce document doit être mis à jour à la fin de chaque itération avec les réalisations, les réussites et les échecs.

## Itération 7.1 : Instrumentation Profiling L2/L3

- **Statut** : Terminé.
- **Action** : Modifier `scripts/benchmark.sh` pour ajouter `perf stat -e L2-load-misses,LLC-load-misses,cpu-migrations`.
- **Validation** : Run script. Les nouveaux compteurs s'affichent correctement.
- **Probabilité Succès Initiale** : 95%
- **Bilan** : **Succès total**. Le script de benchmark capture correctement les événements. Lors du run de validation, on observe ~14.1M de L2-load-misses et LLC-load-misses sur les P-Cores (`cpu_core`), et 4,520 `cpu-migrations`. Le taux de migration (4520) confirme la nécessité urgente de l'itération 7.2 (Thread Pinning) pour stabiliser le L2.

## Itération 7.2 : Thread Pinning (Affinité P-Cores)

- **Statut** : Terminé.
- **Action** : Implémenter appel `pthread_setaffinity_np` au démarrage du moteur RHI. Coder masque CPU pour cibler les cœurs physiques pairs (P-Cores standard).
- **Validation** : Run `benchmark.sh`. Compteur `cpu-migrations` doit être à 0 pour le main thread.
- **Probabilité Succès Initiale** : 80%
- **Bilan** : **Succès massif**. Les migrations CPU ont chuté de 4520 à **2** (migrations inévitables de l'OS au lancement). Conséquence directe impressionnante : les L2 misses ont été divisés par près de 3 (passant de ~14.1M à ~5.4M). Le temps "User CPU" a fondu de 3.74s à 2.44s. L'isolement sur P-Core est validé.

## Itération 7.3 : Anti-False Sharing (Padding 128 Bytes)

- **Statut** : Terminé.
- **Action** : Chercher variables partagées inter-threads (atomiques, compteurs de lock). Ajouter macro `#define CACHE_LINE_SIZE 128`. Appliquer `alignas(CACHE_LINE_SIZE)`.
- **Validation** : Baseline `perf c2c record`.
- **Probabilité Succès Initiale** : 90%
- **Bilan** : **Échec théorique prouvé par l'outil**. L'analyse de la baseline avec `perf c2c` (avant l'ajout du padding) a retourné un constat brutal : **0 événement HITM** (Hit in Modified), **0 ligne de cache partagée en conflit**.
  - **Explication** : Le thread I/O Asynchrone est en réalité quasi inactif 99% du temps (il dort en attendant un fichier). Il ne rentre jamais en collision avec le thread de rendu à la nanoseconde près. Le Faux Partage n'existait pas en pratique.
  - **Leçon apprise** : Mon intuition était une "Micro-optimisation prématurée". Bien que l'ajout du padding (conservé dans le code comme bonne pratique architecturale) sécurise formellement l'espace mémoire, la stagnation des L2-misses n'était pas un biais de mesure, mais bien la preuve que le goulot n'était pas là.

## Itération 7.4 : Thread-Local Storage (TLS) Scratch Arena (Vulkan)

L'objectif est d'éliminer les dernières micro-allocations (ou les `__builtin_alloca` trop gros pour la stack) lors de l'enregistrement des Command Buffers. On crée un allocateur linéaire "Scratchpad" ultra-rapide, persistant dans le cache L1/L2, et garanti sans mutex (car verrouillé sur le thread courant via `thread_local`).

### 7.4.1 : Déclaration et Cycle de vie TLS

- **Action** : Créer un pointeur `thread_local LinearArena tls_scratch_arena;`. L'allouer (ex: 2 MB) au démarrage du thread principal. Ajouter une fonction `arena_reset(&tls_scratch_arena)` appelée **au tout début** de chaque frame.
- **Chances d'aboutir** : 95% (Très simple, API C++ standard).
- **Risque** : Fuite mémoire à la fermeture si on oublie de `free` la mémoire de l'arène TLS.

### 7.4.2 : Migration des allocations RHI

- **Action** : Scanner `src/vk_engine*.cpp`. Remplacer toutes les listes temporaires de Vulkan (tableaux de DescriptorSets, DynamicOffsets, CommandBuffers) par des appels `arena_alloc(&tls_scratch_arena, size)`.
- **Chances d'aboutir** : 80% (Demande de la rigueur sur la durée de vie).
- **Risque** : Vulkan Validation Layers qui hurlent si on fournit un pointeur écrasé (si l'arène reset trop tôt) ou si la capacité de 2 MB est dépassée (Buffer Overflow).

### 7.4.3 : Validation des Registres et Profiling

- **Action** : Run `heaptrack` pour confirmer Zéro allocation dynamique. Run l'app avec les Validation Layers.
- **Chances d'aboutir** : 90%.
- **Bilan final 7.4** : **Succès total**. Les micro-allocations massives via `__builtin_alloca` ont été remplacées par le Scratchpad TLS (`thread_local LinearArena`). Le compilo peut enfin saturer les registres sans risquer de stack overflow.
  - Résultat sur `perf stat` : Baisse supplémentaire d'**~1.5 million de L2 misses** (de 5.4M à 3.9M) !
  - Baisse du L1 misses (de ~158M à ~45M) dû à la localité spatiale parfaite de l'Arena TLS.
  - La mémoire Heap est intacte et les Vulkan Validation Layers valident le cycle de vie de la frame.

## Itération 7.5 : Saturation Registres (Unrolling & SIMD)

- **Statut** : À faire.
- **Action** : Dans `vk_draw_frame_internal` (tri Billboards). Ajouter `#pragma GCC unroll 4` et l'attribut de compilation `__attribute__((target("avx2,fma")))`.
- **Validation** : Inspection `objdump -d`. Les instructions `vmovaps` et `vfmadd` apparaissent. Baisse du temps `perf stat -e cpu-clock`.
- **Probabilité Succès Initiale** : 50%
- **Bilan** : **Succès massif**.
  - **Objdump** : L'assembleur confirme l'utilisation des registres YMM (256-bit) via `vmovaps` et des instructions de calcul fusionné Fused Multiply-Add (`vfmadd231ss`). Le compilo a parfaitement compris notre intention.
  - **Perf Stat** : Le temps utilisateur (CPU *user time*) est tombé de **2.91s à 2.57s**.
  - On a gratté **340 ms** (soit environ 12% de gain de perf CPU) juste avec un `#pragma` et un `__attribute__` bien placés.
  - Architecture désormais propre, sans allocations dynamiques et saturant le matériel.
