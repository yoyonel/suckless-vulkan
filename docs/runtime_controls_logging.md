# Runtime Controls et Logging

Cette page documente les fonctionnalites runtime recentes : controles clavier, bascule fullscreen/fenetre, et systeme de logs structure.

## Controles Runtime

Les controles sont centralises dans `src/runtime_controls.cpp` et exposes via `src/runtime_controls.h`.

Raccourcis clavier :

- `Space` : pause/reprise animation.
- `Up` : acceleration animation (x1.25).
- `Down` : ralentissement animation (x0.8, borne min).
- `R` : reset du temps d'animation.
- `F11` : bascule fullscreen/fenetre.
- `Esc` : fermeture propre de l'application.

Implementation :

- Debounce clavier via `runtime_is_key_pressed_once`.
- Bascule fullscreen testable via `WindowOps` (injection des operations GLFW).
- La fermeture est demandee via `glfwSetWindowShouldClose`, puis la boucle sort et appelle le cleanup Vulkan.

## Fullscreen et Swapchain

Le passage fullscreen/fenetre change la taille de surface; le moteur gere :

- `VK_ERROR_OUT_OF_DATE_KHR` a l'acquisition d'image.
- `VK_ERROR_OUT_OF_DATE_KHR` et `VK_SUBOPTIMAL_KHR` a la presentation.
- Recreation des ressources dependantes du swapchain (swapchain, renderpass, pipeline, depth, framebuffers).

Ce comportement est implemente dans `src/vk_engine.cpp`.

## Logging Structure

Le logger est implemente dans :

- `src/app_log.h`
- `src/app_log.cpp`

Fonctionnalites :

- Niveaux: `DEBUG`, `INFO`, `WARNING`, `ERROR`, `CRITICAL`.
- Prefix horodate + pid/tid + tag + niveau.
- Filtrage par niveau global.
- Callback optionnel via `log_set_callback`.

Variables d'environnement :

- `VULKAN_LOG_LEVEL`
- `OGL_LOG_LEVEL` (compatibilite)

Exemple :

```bash
VULKAN_LOG_LEVEL=DEBUG just run
```

## Tests Associes

Tests d'integration :

- `just test-integration`

Tests logiques (controles + logger) :

- `just test-logic`

Flow complet recommande :

- `just test-all`
