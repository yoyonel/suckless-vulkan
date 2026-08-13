# Suivi des Benchmarks Heaptrack (Allocations)

**Date** : 2026-08-13

Ce document trace l'évolution du nombre d'allocations dynamiques sur le chemin critique du moteur, profilé avec `heaptrack` via la recette d'intégration `just benchmark-heaptrack` (utilisant un framebuffer virtuel Xvfb et des scénarios scénarisés via `xdotool`).

## Scénario d'intégration (interactive_runner.sh)

1. Démarrage de l'application (Headless, Lavapipe software rasterizer)
1. `Page_Down` (via xdotool) -> Lancement asynchrone du pre-baking IBL (Changement HDR).
1. Attente 5s.
1. `Page_Down` (via xdotool) -> Second lancement IBL.
1. `Escape` -> Extinction propre de l'application.

## Benchmark du 2026-08-13 (Chantier 3 - Logs & Labels Zero Allocation)

**Objectif** : Validation empirique de l'absence de fragmentation du tas (`std::string`) par le système de Logs et les Labels Debug Vulkan, participant à l'objectif de "DRAM bound < 8%".

### Résultats (Extrait `heaptrack_results.txt`)

- **Peak heap memory consumption**: 36.01M
- **Calls to allocation functions**: ~2400 allocations
- **Total memory leaked**: 35.91M (Essentiellement des singletons/RHI arena désalloués en bloc à la fermeture par l'OS)

### Top Allocations (Bottom-Up)

Les 2400 allocations restantes sont majoritairement issues de l'initialisation du stack graphique X11 (`libX11.so.6`) et de `glfwInit`.

**ZÉRO allocation** identifiée dans la boucle principale provenant de :

- `app_log`
- `vk_engine_envmap` (`get_filename_from_path`)
- `vkSetDebugUtilsObjectNameEXT`

### Conclusion

La rustine supprimant les concaténations `std::string` pour `log_format()` est validée de bout-en-bout. Les allocations du moteur en `hot-path` lors du basculement d'environnement IBL sont nulles côté logging.
