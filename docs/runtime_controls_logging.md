# Runtime Controls et Logging

Cette page documente les fonctionnalites runtime recentes : controles clavier, bascule fullscreen/fenetre, camera souris, skybox HDR fullscreen, et systeme de logs structure.

## Controles Runtime

Les controles sont repartis entre :

- `src/runtime_controls.cpp` / `src/runtime_controls.h` pour les controles systeme (pause, vitesse, fullscreen, fermeture).
- `src/vk_engine_runtime.cpp` pour les controles camera/skybox et callbacks GLFW.
- `src/vk_engine_frame.cpp` pour l'integration par frame (`vk_handle_runtime_input`, camera, rendu).

Raccourcis clavier :

- `Space` : pause/reprise animation.
- `Up` : acceleration animation (x1.25).
- `Down` : ralentissement animation (x0.8, borne min).
- `R` : reset du temps d'animation.
- `F5` : rechargement dynamique du module RHI (hot-reload).
- `F6` : active/desactive le mode debug IBL (Irradiance map).
- `F11` : bascule fullscreen/fenetre.
- `Esc` : fermeture propre de l'application.
- `C` : active/desactive la capture souris pour la camera.
- `W/A/S/D/Q/E` : deplacement camera (avant/arriere/strafes/haut/bas).
- `Molette` : impulse cinétique (ajoute momentum au mouvement camera).
- `K` : affiche/cache la skybox HDR.
- `PageUp` : passe a l'envmap HDR suivante.
- `PageDown` : passe a l'envmap HDR precedente.
- `Shift+PageUp` : augmente le LOD de sampling de l'envmap.
- `Shift+PageDown` : diminue le LOD de sampling de l'envmap.

Implementation :

- Debounce clavier via `runtime_is_key_pressed_once`.
- Bascule fullscreen testable via `WindowOps` (injection des operations GLFW).
- Camera souris via callbacks GLFW (`glfwSetCursorPosCallback`, `glfwSetScrollCallback`) et integration avec `src/camera.cpp`.
- Skybox HDR fullscreen via pipeline dedie (`shaders/skybox.vert`, `shaders/skybox.frag`) + catalogue des `.hdr` charges depuis `assets/textures/hdr`.
- Changement d'envmap runtime avec decode async (thread IO) puis activation GPU sur le thread render + mise a jour du descriptor set Vulkan.
- La fermeture est demandee via `glfwSetWindowShouldClose`, puis la boucle sort et appelle le cleanup Vulkan.

## Systeme de Camera Cinetique

La camera utilise un systeme de physique avec **momentum et friction** pour un mouvement fluide et naturel, entièrement aligné avec le legacy `suckless-ogl`.

**Voir [Caméra Cinétique](kinetic_camera.md) pour la documentation complète et détaillée.**

### Survol Rapide

- **Touches WASD/QE** : définissent une `targetVelocity` (direction + vitesse max).
- **Molette souris** : ajoute une **impulsion cinétique** à la vélocité courante.
- **Interpolation** : la `velocityCurrent` converge progressivement vers `targetVelocity` via absorption (accélération = 10.0).
- **Friction** : quand aucune touche n'est pressée, la vélocité s'atténue par 85% pour ralentir naturellement.
- **Intégration** : `position += velocityCurrent * deltaTime`.

### Paramètres Inherited du Legacy

- Distance initiale: `20.0f`
- Vitesse mouvement: `15.0f` units/sec
- Sensibilité souris: `0.15f`
- Rotation smoothing: `0.18f`
- Accélération WASD: `10.0f`
- Friction: `0.85f`
- Impulsion scroll: `50.0f` units par clique
- FOV skybox: `60°` (fixe)
- Planes: near `0.1`, far `1000.0`

### Sensation de Mouvement

1. **Appui W** → accélération lisse vers vitesse max.
1. **Relâche W** → décélération progressive via friction (pas d'arrêt sec).
1. **Scroll montant** → impulsion immédiate dans direction avant + inertie.
1. **Scroll + WASD** → impulsion ajoute à mouvement habituel (effets cumulatifs).

Cet effet procure une **sensation de poids et d'inertie physique** sans code de physique complexe.

## Fullscreen et Swapchain

Le passage fullscreen/fenetre change la taille de surface; le moteur gere :

- `VK_ERROR_OUT_OF_DATE_KHR` a l'acquisition d'image.
- `VK_ERROR_OUT_OF_DATE_KHR` et `VK_SUBOPTIMAL_KHR` a la presentation.
- Recreation des ressources dependantes du swapchain (swapchain, renderpass, pipeline, depth, framebuffers).

Ce comportement est implemente dans `src/vk_engine.cpp`.

Depuis la decomposition du moteur, il est surtout reparti entre :

- `src/vk_engine_init.cpp` pour la recreation swapchain/pipeline.
- `src/vk_engine_frame.cpp` pour la detection `OUT_OF_DATE/SUBOPTIMAL` pendant draw/present.

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

## Options CLI

L'exécutable (`vulkan_app`) accepte les arguments suivants :

- `--no-vsync` : Désactive le VSync.
- `--vsync` : Force le VSync.
- `--nullrhi` : Démarre avec le NullRHI.
- `--no-focus` : Empêche la fenêtre de prendre le focus, cache la fenêtre (off-screen) et désactive la capture de la souris au démarrage. Utilisé principalement pour l'exécution silencieuse des tests.

## Tests Associes

Tests d'integration :

- `just test-integration`

Tests logiques (controles + logger) :

- `just test-logic`

Flow complet recommande :

- `just test-all`
