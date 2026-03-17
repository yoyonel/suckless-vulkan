# Architecture du Moteur Vulkan

Le fichier `vk_engine.cpp` orchestre l'initialisation complète de l'API Vulkan. Contrairement à OpenGL qui gère une machine à états globale, Vulkan exige la création explicite de chaque composant du pipeline.

## ⚙️ Séquence d'Initialisation

1. **Core (`init_core`) :**

   - Initialisation de GLFW (`GLFW_NO_API`).
   - Création de la `VkInstance` (Connexion entre l'application et le driver Vulkan).
   - Création de la `VkSurfaceKHR` (Le lien entre Vulkan et le système de fenêtrage de l'OS).
   - Sélection d'un `VkPhysicalDevice` compatible avec la surface, les extensions requises et les files de commandes nécessaires.
   - Création du `VkDevice` (GPU logique) et récupération des files graphique et de présentation.

1. **Allocateur (`init_allocator`) :**

   - Initialisation du contexte VMA (Vulkan Memory Allocator).

1. **Swapchain (`init_swapchain`) :**

   - Création de la file d'attente d'images qui feront le va-et-vient entre le GPU et l'écran.
   - Sélection d'un format de surface, d'un mode de présentation et d'une extent compatibles avec le système courant.
   - *Note historique :* Sous certains gestionnaires de fenêtres (comme Compiz/X11), une fenêtre ne s'affiche pas tant qu'une première image n'a pas été présentée. La Swapchain est donc obligatoire pour que la fenêtre GLFW devienne visible.

1. **Render Pass (`init_render_pass`) :**

   - Définition de la "passe de rendu" qui dicte comment les images de la Swapchain doivent être traitées (ex: effacer l'écran en bleu à chaque début de frame).

1. **Graphics Pipeline (`init_pipeline`) :**

   - Configuration de l'état global du GPU (Viewport, Rasterizer, Blending).
   - Définition de la structure des sommets : deux bindings vertex — `RATE_VERTEX` (géométrie) et `RATE_INSTANCE` (offsets d'instancing).
   - Chargement et compilation des shaders SPIR-V (`.spv`).

1. **Buffers Géométriques et d'Instancing (`init_buffers`) :**

   - Création du *Vertex Buffer* (sommets de la sphère) et de l'*Index Buffer* via VMA en VRAM.
   - Création de l'*Instance Buffer* : 100 positions `glm::vec3` en grille 10 × 10, uploadé en GPU-only via staging buffer.
   - Création de l'UBO persistant (`CPU_TO_GPU`) contenant `vp` + `modelRotation`.

1. **Synchronisation (`init_commands_and_sync`) :**

   - Création des `VkSemaphore` (Synchro GPU-GPU) et `VkFence` (Synchro CPU-GPU) pour éviter les chevauchements de frames.

## 🔒 Robustesse

L'implémentation vérifie explicitement les retours des principales créations Vulkan et VMA, afin d'échouer proprement en cas de plateforme incompatible ou de ressource manquante.
