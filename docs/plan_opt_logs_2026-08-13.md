# Plan d'Optimisation des Logs et Labels Vulkan

**Date** : 2026-08-13

## 1. Cartographie Legacy (Allocations Dynamiques)

### Logs Purs (`.c_str()`)

1. `src/vk_engine_envmap.cpp`: Appels multiples avec `.c_str()` sur `request.sourcePathOrLabel`, `ktxPath`, `engine->hdrFiles[i]`.
1. `src/vk_engine_envmap.cpp`: `get_filename_from_path(...)` génère une `std::string` temporaire avant `.c_str()`.
1. `src/vk_engine_ibl.cpp`: `ec.message().c_str()` alloue.
1. `src/main.cpp` / `module_loader.cpp`: `libName.c_str()` et `path.c_str()`.

### Labels Vulkan (Générateurs de L3/RAM traffic)

1. `src/rhi/vulkan_rhi.cpp`: Concaténations `std::string(name) + "_Staging_Buffer"` (x5 endroits).
1. `src/swapchain_init.cpp`: `"Swapchain_ImageView_" + std::to_string(i)`.
1. `src/vk_engine_init.cpp`: `"Swapchain_Framebuffer_" + std::to_string(i)`.

## 2. Plan de Remplacement

1. **Réfracto Utilitaires** : Modifier `get_filename_from_path` pour retourner un `const char*` (via `log_format` ou `std::string_view`).
1. **Substitution Logs** : Remplacer les passages par copie/allocation de strings par un accès direct (si durée de vie OK) ou via `log_format("%s", data)`.
1. **Substitution Labels Vulkan** : Remplacer `std::string(...) + "..."` par `log_format("%s_Suffix", name)`.
1. **Substitution Indexation** : Remplacer `... + std::to_string(i)` par `log_format("Prefix_%d", i)`.

## 3. Plan de Test (Validation Fonctionnelle)

1. **Intégrité Compilation** : `just build` pour valider les conversions `const char*`.
1. **Intégrité Mémoire (Crucial)** : `just test-asan` pour s'assurer que le ring buffer (thread_local) ne génère pas de data races ou d'Use-After-Free sur les logs différés.
1. **Intégrité Logique** : Vérifier visuellement la sortie console pour l'absence de chaînes tronquées.

## 4. Plan d'Observation (Validation Profiling / 8% DRAM)

1. **Outil 1 - Heaptrack (Vérification Zéro Allocation)** :
   - Lancer : `heaptrack ./build/release/vulkan_app`.
   - Action : Vérifier l'onglet "Allocations" -> Les appels à `std::string` ou `snprintf` depuis `vk_engine_*` et `app_log` doivent être à **0**.
1. **Outil 2 - VTune (Mesure DRAM Bound)** :
   - Lancer : `just benchmark-vtune` (en root, défini dans le Protocole de Validation de la spec).
   - Action : Comparer les métriques matérielles. Vérifier que *Memory Bound > DRAM Bound* approche les 8% et que les *LLC Misses* (L3) baissent significativement (grâce à l'absence de fragmentation du tas).
1. **Outil 3 - Tracy (Optionnel)** :
   - Vérifier la ligne de vie "Memory" pour confirmer l'aplatissement de la courbe des allocations pendant les phases IBL/Envmap.
