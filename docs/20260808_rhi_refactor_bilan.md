# Bilan Exhaustif : Branche `feature/rhi-refactor` (2026-08-08)

## 1. RÉALISATIONS MAJEURES (Ce qui a été fait)

- **Abstraction RHI (Phases 3 à 8) :** Extraction totale de l'API Vulkan hors de la logique métier. Création de l'interface `IRHI`.
- **Ressources Opaques :** Remplacement des pointeurs bruts (`VkBuffer`, `VkImage`) par des Handles opaques (`BufferHandle`, `TextureHandle`, `PipelineHandle`).
- **NullRHI (Phase 5) :** Création d'un backend graphique fantôme pour l'exécution sur serveur headless (tests CI).
- **Commandes Agnostiques (Phase 8.5) :** Abstraction des CommandBuffers et DescriptorSets derrière `IRHI`. Le code de rendu IBL et Geometry ne connaît plus Vulkan.
- **Hot-Reload DLL (Phase 9) :** Découplage de la RHI en bibliothèques dynamiques (`libvulkan_rhi.so`, `libnull_rhi.so`). Implémentation du rechargement à chaud (F5) via `dlopen`/`dlsym`.
- **Garde-fous (Tests) :** Validation Golden Image stricte pour éviter les régressions visuelles silencieuses, et scripts de Smoke Tests CI avec timeout.

______________________________________________________________________

## 2. LE POSITIF (Ce qui est bien fait / Solide)

- **Codebase découplée :** La logique applicative (Inputs, Caméra cinétique, Temps) est enfin isolée du hardware. Le `CoreEngine` est sain.
- **Vitesse d'itération :** Le hot-reload F5 permet de recompiler uniquement la RHI (shaders/API) et l'injecter à chaud sans perdre le contexte (`EngineState`).
- **Tests blindés :** Les tests fonctionnaient "en aveugle" auparavant. L'ajout des Golden Images au pixel près (Phase 8.5) est un filet de sécurité critique.
- **NullRHI en CI :** Permet une validation de toute la logique de jeu sur n'importe quelle machine GitHub Actions, sans GPU.
- **Couverture LLVM > 84% :** Rigueur impressionnante sur la santé du code.

______________________________________________________________________

## 3. LE NÉGATIF / DETTE TECHNIQUE (Ce qui est mal fait / À corriger)

### A. Abstraction trop "Vulkan-Centric" (Leaky Abstraction)

- `IRHI` masque Vulkan, mais sa forme calque exactement l'API Vulkan (ex: `CmdPipelineBarrier`, gestion manuelle des `DescriptorPool`). Un vrai moteur moderne utilise une API plus haut niveau (ex: Graphes de Rendu / Render Graphs). Si on veut porter vers DirectX12 ou Metal, l'interface `IRHI` forcera des pirouettes.

### B. "God Object" `EngineState`

- Le refactoring a poussé tout le contexte dans `EngineState` passé par pointeur partout. C'est pratique pour le Hot-Reload (pour ne pas perdre la RAM), mais c'est un anti-pattern orienté objet. Cela crée un couplage fort entre des systèmes qui n'ont rien à voir.

### C. Typage des Handles (Sécurité Mémoire GPU)

- Les `BufferHandle` / `TextureHandle` sont des identifiants (probablement des entiers). Il manque un système robuste de *Reference Counting* (RAII). Si la logique métier perd un Handle sans le détruire, la RHI garde la VRAM allouée (Fuite GPU).

### D. Intégration Profiler (Tracy) "Duct-Tape"

- Tracy est statiquement linké. Avec des `.so` multiples (ex: `main` + `vulkan_rhi.so`), Tracy crée des contextes en double, ce qui crashe le tracking mémoire. Le hack a été d'isoler l'init Tracy dans l'exécutable, mais on perd une instrumentation fine "inside" la RHI.

### E. Dépendance au Linter & Temps de CI

- La division par DLL + CMake est propre, mais le build devient complexe. `main.cpp` reste un bloc "fourre-tout" (CLI args, Init GLFW, Chargement RHI) qui flirte constamment avec la limite de "Complexité Cognitive = 25".

______________________________________________________________________

## Conclusion du Bilan

Le refactoring est un succès majeur qui fait passer le projet d'un "tuto Vulkan monolithique" à un **moteur structuré et modulaire**. Néanmoins, le système de gestion de la mémoire GPU (Handles) et l'interface RHI (trop bas-niveau) constitueront les prochains plafonds de verre architecturaux.
