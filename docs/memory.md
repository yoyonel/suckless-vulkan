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
