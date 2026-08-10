# Gestion de la Mémoire : Vulkan Memory Allocator

L'une des plus grandes complexités de Vulkan est sa gestion manuelle de la mémoire. Il incombe au développeur de demander de grands blocs de mémoire à l'OS (`VkDeviceMemory`), de gérer la pagination, et de respecter les contraintes d'alignement strictes dictées par l'architecture physique du GPU.

Pour éviter de recoder un gestionnaire de mémoire complet, le moteur intègre **Vulkan Memory Allocator (VMA)**, développé par AMD.

## 🔄 Le Choix du C++

VMA expose une API C (`vmaCreateBuffer`, `vmaMapMemory`), mais son implémentation interne (`vma_impl.cpp`) repose fortement sur le C++ moderne (templates, `std::mutex`, etc.).

Le projet a donc été migré vers le standard **C++17** pour assurer la compatibilité.

- *Conséquence :* Les structures Vulkan sont désormais initialisées en utilisant la syntaxe par défaut du C++ (`VkInstanceCreateInfo createInfo{};`) au lieu de la syntaxe C99 (`VkInstanceCreateInfo createInfo = {0};`), garantissant un code sûr et sans avertissement du compilateur.

## 📦 Création d'un Vertex Buffer

VMA réduit drastiquement le "boilerplate" nécessaire pour allouer un buffer. Voici comment nos géométries sont envoyées à la VRAM :

1. Définition des paramètres du buffer (Taille, Usage).
1. Définition des contraintes VMA (ex: `VMA_MEMORY_USAGE_CPU_TO_GPU` pour une mémoire modifiable par le CPU mais optimisée pour le GPU).
1. Allocation unique via `vmaCreateBuffer`.
1. Mapping de la mémoire (`vmaMapMemory`), copie des données (`memcpy`), et Unmapping (`vmaUnmapMemory`).

## 🧠 Gestion de la Mémoire CPU (DOD)

Contrairement aux moteurs orientés objet classiques qui s'appuient sur l'allocation dynamique granulaire (`malloc` / `new`), Suckless Vulkan adopte une architecture **Data-Oriented Design (DOD)** pour le CPU.

### Le Problème du Tas (Heap)

L'utilisation de `new` ou de pointeurs intelligents (`std::shared_ptr`, `std::unique_ptr`) provoque :

- **Fragmentation de la mémoire** : Ralentissement global de l'OS.
- **Cache Misses L1/L2** : Les objets sont éparpillés dans la RAM, détruisant la prédictibilité pour le CPU.
- **Overhead de Tracking** : Gérer la durée de vie coûte des cycles d'horloge.

### La Solution : LinearArena (Bump Allocator)

La gestion mémoire CPU du moteur repose entièrement sur l'allocateur linéaire (`LinearArena`), qui alloue de gros blocs continus au démarrage de l'application et les redistribue par incrémentation d'un offset (`placement new`).

#### L'Architecture des Arenas Globales

- `core.arena` (32 MB) : Réservée au CoreEngine (Game state, Billboards, Caméra).
- `rhiArena` (2 MB) : Dédiée à l'instanciation des modules RHI dynamiques (`VulkanEngine`, `VulkanRHI`).

#### Avantages de l'implémentation

1. **O(1) Allocation :** L'allocation se résume à `offset += size`.
1. **Localité spatiale absolue :** Les composants (ECS/RHI) sont adjacents. Le L1-Cache est maximisé.
1. **Zéro Memory Leak :** Aucune désallocation unitaire n'est supportée. Toute la mémoire est rendue à l'OS en un seul bloc à la fermeture de l'application (`arena_free`), ou au rechargement de module (Hot-Reload F5) en remettant l'offset à `0`.

> [!TIP]
> **Hot-Reloading** : Séparer l'état logique (`core.arena`) de l'état RHI (`rhiArena`) permet de réinitialiser l'offset de l'arène graphique et d'y recharger une nouvelle `.so` en conservant l'état du jeu intact.

## 🚀 Prochaines Étapes : L2/L3, RAM & Faux Partage (False Sharing)

La gestion actuelle est optimisée pour le cache L1 (alignement 64 bytes) et masque la latence RAM grâce au hardware prefetcher induit par le DOD (Itérations 3 à 5).
Pour l'**Itération 7**, l'architecture mémoire doit s'étendre aux couches inférieures :

1. **Isolement L2/L3 et Thread Affinity (Pinning) :**

   - L'architecture matérielle hybride (P-Cores / E-Cores) induit des caches L2 non-uniformes. Laissent l'OS migrer les threads (Thread Migration) détruit le L2 et provoque des pénalités massives de "Cache-to-Cache Transfer" (HITM, jusqu'à 126ns).
   - **Solution :** Verrouiller les threads RHI sur les P-Cores (`pthread_setaffinity_np`) et isoler l'I/O (Asset Streaming KTX2) sur les E-Cores.

1. **Padding Anti-Prefetcher Spatial (128 Bytes) :**

   - Deux threads travaillant sur des données adjacentes dans le cache L2 se parasitent mutuellement (False Sharing).
   - **Solution :** Étendre l'alignement (`alignas`) de 64 octets à 128 octets spécifiquement pour les structures partagées (Compteurs, verrous, atomiques).

1. **Arenas Locales par Thread (TLS) :**

   - L'enregistrement concurrent des Command Buffers Vulkan nécessite l'absence totale de mutex.
   - **Solution :** Une instance de `LinearArena` stricte par Thread d'enregistrement, mappée sur des blocs `VulkanDeviceMemory` dédiés pour éviter de verrouiller le Ring Bus mémoire au niveau matériel.

1. **Registres et SIMD (AVX2/AVX-512) :**

   - Éviter au maximum les allers-retours vers le L1.
   - **Solution :** Forcer le déchargement de boucles (`#pragma GCC unroll`) et utiliser des Intrinsics vectorielles explicites (`__m256`, `__m512`) pour conserver les données dans les registres YMM/ZMM lors des calculs denses (tri de particules, matrices).
