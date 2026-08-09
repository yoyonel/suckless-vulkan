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
