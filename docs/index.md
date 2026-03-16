# Accueil : Moteur Vulkan Custom

Bienvenue dans la documentation du moteur Vulkan développé de zéro.

L'objectif de ce projet est de construire une architecture de rendu 3D robuste, performante et hautement contrôlable, en s'appuyant sur l'API bas niveau Vulkan.

## 🛠️ Stack Technique

Afin de garantir un compromis idéal entre contrôle absolu et pragmatisme de développement, les choix technologiques suivants ont été arrêtés :

- **Langage :** C++17. (Migration depuis le C11 pour supporter *Vulkan Memory Allocator*).
- **API Graphique :** Vulkan 1.0 (via `libvulkan.so`).
- **Fenêtrage :** GLFW 3 (`glfw3`).
- **Allocation Mémoire :** Vulkan Memory Allocator (VMA) d'AMD.
- **Build System :** CMake.
- **Task Runner :** Just (`justfile`).
- **Shaders :** GLSL, compilés en SPIR-V via `glslc`.

## 📁 Architecture du Code

```text
vulkan_base/
├── CMakeLists.txt      # Configuration de compilation principale
├── justfile            # Définition des recettes (build, run, lint, format)
├── ext/
│   ├── stb/            # Dépendances externes header-only, référencées explicitement depuis ext/
│   └── vma/            # Dépendances externes, référencées explicitement depuis ext/
├── shaders/            # Code source des shaders (GLSL)
├── src/
│   ├── main.cpp        # Point d'entrée, gestion de la boucle GLFW
│   ├── vk_engine.cpp   # Implémentation du moteur Vulkan
│   └── vk_engine.h     # Définitions et structure `VulkanEngine`
└── tests/
    └── test_main.cpp   # Test d'intégration du rendu et de la capture de frame
```
