# RHI Refactoring - Phase 2: CoreEngine & InputState

## Objectif

Séparer la logique métier (calcul du temps, gestion de la caméra, traitement des inputs utilisateur, états de rendu) des appels systèmes (GLFW) et graphiques (Vulkan).

## Décisions d'Architecture

### 1. Création de `CoreEngine`

Un objet pur, agnostique de Vulkan et de GLFW, contenant tout l'état de la logique du moteur (`src/core_engine.h/cpp`).

- **Initialisation** : `core_engine_init(CoreEngine*)` initialise les variables (temps, paramètres PBR, caméra) sans aucun appel à `VulkanEngine`.
- **Mise à jour** : `core_engine_update(CoreEngine*, const CoreInput*, float)` est responsable du calcul du DeltaTime (avec *clamp*), de l'évolution de la simulation (`animationTimeSeconds`), et de la modification de l'état suite aux inputs.

### 2. Le pattern `CoreInput`

La couche GLFW envoie des événements (callback souris/scroll, appuis clavier) qui étaient auparavant traités directement via des références à la `VulkanEngine`.
Pour briser cette dépendance :

- Une structure `CoreInput` a été définie (agnostique de GLFW). Elle stocke de manière éphémère (à l'échelle d'une frame) l'état des actions (ex: `pausePressed`, `mouseDeltaX`).
- **Couche Système (`vk_engine_runtime.cpp` / `runtime_controls.cpp`)** : Ces fichiers s'occupent uniquement de lire GLFW et de peupler un objet `CoreInput currentInput`. Les callbacks souris accumulent les deltas dans cet objet.
- **Couche Logique (`core_engine_update`)** : Le `CoreEngine` lit cet objet et applique les transformations nécessaires (ex: `camera_process_mouse`).
- **Isolation** : À la fin de la frame, les accumulations (ex: `mouseDeltaX`) sont remises à zéro par le système appelant (la boucle principale).

### 3. Gestion des états liés à l'OS/Backend (VSync, Fullscreen)

Les actions déclenchant directement des modifications de la Swapchain ou de la fenêtre native sont conservées hors de `CoreEngine`. `vk_handle_runtime_input` et `runtime_toggle_fullscreen` interceptent ces actions spécifiques (ex: `GLFW_KEY_V` pour VSync, appelant `vk_recreate_swapchain`) sans les remonter dans la logique pure.

### 4. Outil de validation de la Pureté (`check_rhi_leaks.py`)

Un hook pre-commit garantit que les fichiers du `CoreEngine` n'incluent jamais `vulkan.h` ou des types propres au backend (`VkImage`, `VmaAllocation`).
