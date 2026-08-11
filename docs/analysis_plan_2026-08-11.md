# Analyse et Plans d'Implémentation - suckless-vulkan (2026-08-11)

## Outils Internes de Validation & Profiling Disponibles (Justfile)

Le projet dispose d'une infrastructure très complète pour garantir l'absence de régression. Tout changement doit utiliser ces outils :

- **Tests de Non-Régression Rendu** : `just sync-test-references-all` (Docker CI, comparaison bit à bit des frames).
- **Tests Logiques & Intégration** : `just test-all`, `just test-logic`, `just test-integration-tracy`.
- **Sanitizers & Vulkan** : `just test-asan`, `just test-validation-layers`.
- **Profiling Framerate** : `just benchmark-analyze` (capture Tracy auto + export CSV + `analyze_fps.py` & `analyze_worst_frames.py` pour détecter les stutters).
- **Cache Profiling (L1/LLC)** : `just perf-benchmark` (utilise Linux `perf stat` pour mesurer les cache-misses).
- **Validation Architecture** : `just check-rhi-leaks` (script python validant l'étanchéité de l'abstraction RHI).
- **Validation IBL** : `just verify-ibl` (comparaison mathématique avec l'implémentation OpenGL).

______________________________________________________________________

## 1. God-Objects (VulkanEngine & vk_engine_init.cpp)

**Analyse** : `VulkanEngine` (311 lignes) centralise tout : RHI, Swapchain, IO, IBL, UI. `vk_engine_init.cpp` (1300+ lignes) mixe l'initialisation Vulkan, UI et Render. Fort couplage cyclique via `EngineState* appState`.
**Plan (Refactoring)** :

- Créer des modules distincts : `SwapchainManager`, `IblBaker`, `RendererContext`.
- Remplacer le pointeur global `VulkanEngine*` par une Injection de Dépendances (Interfaces RHI).
- Scinder `vk_engine_init.cpp` en fichiers ciblés (`rhi_init.cpp`, `swapchain_init.cpp`).
  **Évaluations** :
- **Gains** : Architecture testable (mocks), cohésion forte.
- **Risques** : Régressions graves sur le cycle de vie Vulkan (fences bloquées, crash destruction ordre).
- **Coût** : Élevé (2-3 jours).
  **Tests & Profiling obligatoires** :
- `just test-integration-tracy` (Valide le cycle complet d'initialisation et destruction via capture Tracy).
- `just test-validation-layers` (Traque les erreurs de cycle de vie Vulkan).
- `just test-oom` (Valide la robustesse des nouvelles structures).

**Blocs d'Itérations (Branche `refactor/god-objects`)** :

- **Bloc 1 : Extraction SwapchainManager (`swapchain_init.cpp`)**
  - **Action** : Extraire la logique de cycle de vie de la swapchain (création, destruction, récréation) de `VulkanEngine` vers une classe `SwapchainManager`. Séparer dans `swapchain_init.cpp/h`.
  - **Valeur ajoutée** : Cohésion forte, séparation des responsabilités. Le Swapchain n'est plus mêlé à l'initialisation du pipeline graphique.
  - **KPI** : Aucun accès direct à `engine->swapchain` depuis l'extérieur de `SwapchainManager`. Réduction de la taille de `vk_engine_init.cpp`.
  - **Tests & Évaluation** : `just test-integration-tracy` (passe avec succès). `just test-validation-layers` (0 erreur Vulkan à la création/destruction).
  - **Benchmark/Profiling** : Validation des temps d'initialisation via logs Tracy.
- **Bloc 2 : Extraction IblBaker (`vk_engine_ibl.cpp`)**
  - **Action** : Extraire la logique de calcul de l'IBL (Irradiance, Specular, BRDF LUT) de `VulkanEngine` vers une classe dédiée `IblBaker`.
  - **Valeur ajoutée** : Isolation du code de pre-calcul. Facilite la gestion de la mémoire et limite la pollution de la structure principale.
  - **KPI** : Suppression des variables IBL globales de pre-processing dans `VulkanEngine`.
  - **Tests & Évaluation** : `just verify-ibl` valide mathématiquement que les maps générées sont identiques.
  - **Benchmark/Profiling** : Analyse de la mémoire allouée (spikes) lors du bake via Tracy Memory Profiler.
- **Bloc 3 : RendererContext & Dependency Injection (`vk_engine_runtime.cpp`)**
  - **Action** : Créer un `RendererContext` abstrait et retirer le God-Object `VulkanEngine*` des paramètres de rendu.
  - **Valeur ajoutée** : Couplage faible, abstraction RHI renforcée, testabilité accrue par mocks.
  - **KPI** : Zéro `#include <vulkan/vulkan.h>` dans `vk_engine_runtime.cpp` et `vk_engine_frame.cpp`.
  - **Tests & Évaluation** : `just check-rhi-leaks` (zéro fuite de headers backend).
  - **Benchmark/Profiling** : `just benchmark-analyze` valide l'absence d'overhead significatif lié aux virtual calls.

**Validation Finale (Post-Bloc 3)** :

- Tests COMPLETS : unitaires, fonctionnels, intégrations, end2end.
- Benchmark et profiling COMPLETS : analyses de traces Tracy Profiler, grep de logs applicatifs pour surveiller les temps de chargement, analyse de l'utilisation de la mémoire et des caches L1/L2/L3/RAM et des Misses Caches via `perf`.

______________________________________________________________________

## 2. Retours Booléens au lieu de Structures (Result)

**Analyse** : Méthodes comme `has_required_device_extensions` ou `select_physical_device` retournent `bool`. Perte silencieuse du contexte d'erreur. Les types natifs `AppResult`/`GfxResult` (dans `result.h`) sont ignorés.
**Plan (Refactoring)** :

- Remplacer `bool` par `std::expected<T, GfxResult>` (si C++23) ou `Result<T>` custom.
- Propager les erreurs (ex: `ErrorOutOfMemory`, `ErrorUnsupportedFeature`) au `main.cpp`.
  **Évaluations** :
- **Gains** : Logs explicites, debug instantané, résilience.
- **Risques** : Inversions logiques (`!res` vs `res.has_value()`).
- **Coût** : Faible (1 jour).
  **Tests & Profiling obligatoires** :
- `just test-logic` (Valide la propagation d'erreurs pures).
- `just test-asan` (Valide qu'aucun chemin d'erreur ne leak de la mémoire Vulkan).

**Blocs d'Itérations (Branche `refactor/result-bools`)** :

- **Bloc 1 : Initialisation Engine (`vk_engine_init.cpp`)**
  - **Action** : Remplacer `bool` par le type `GfxResult` (défini dans `result.h`) pour `has_required_device_extensions`, `select_physical_device`, `device_supports_swapchain`.
  - **MVP** : Le core engine retourne une raison formelle d'échec (ex: `ErrorUnsupportedFeature`) propagée jusqu'à `main.cpp`.
  - **KPI** : `just test-logic` passe. Les logs affichent le type d'erreur exact via `to_string(GfxResult)` au lieu de "Init failed".
- **Bloc 2 : Runtime / Input (`vk_engine_runtime.cpp`)**
  - **Action** : Retirer `bool` sur l'input handling. Utiliser des énumérations explicites (ex: `InputState::PressedOnce`).
  - **MVP** : Sémantique claire sans ambiguïté sur l'état des touches.
  - **KPI** : `just test-integration-tracy` passe (interaction UI/Input intacte).

______________________________________________________________________

## 3. Allocations STL Dynamiques (std::vector)

**Analyse** : `std::vector` alloués/désalloués dans des chemins critiques :

- `vk_engine_ibl.cpp` : `floatData` & `finalData` causent de grosses allocations sur le Tas lors du processing.
- `app_log.cpp` : `message` (buffer dynamique).
- `vulkan_rhi.cpp` : Création de vectors par frame pour les descriptor bindings.
  **Plan (Optimisation)** :
- Remplacer par `std::array` (taille compile-time) ou `alloca()` pour les buffers temporaires (ex: layout bindings).
- Pré-allouer (réservation capacité) ou réutiliser les vectors pour l'IBL et les logs via un allocateur persistant (`std::pmr::monotonic_buffer_resource`).
  **Évaluations** :
- **Gains** : Réduction massive de la fragmentation mémoire, suppression des micro-stutters (framerate stable), baisse drastique des L1/LLC Cache Misses.
- **Risques** : Stack overflow si fallback dynamique manquant.
- **Coût** : Moyen (1-2 jours).
  **Tests & Profiling obligatoires** :
- `just benchmark-analyze` (Valide que l'analyseur Python ne remonte plus de worst-frames/spikes).
- `just perf-benchmark` (Vérifier la baisse effective des compteurs `L1-dcache-load-misses` et `LLC-load-misses`).
- Analyse visuelle des allocations dynamiques via le profil de mémoire dans l'UI du Tracy Profiler.

**Blocs d'Itérations (Branche `opt/stl-allocs`)** :

- **Bloc 1 : RHI Hot Path (`vulkan_rhi.cpp`)**
  - **Action** : Remplacer `std::vector` par `std::array` (tailles fixes 16/32 max) ou `alloca` (C-style stack) dans `UpdateDescriptorSet` et `BindPipeline`.
  - **MVP** : 0 allocation dynamique (tas/heap) par frame dans les appels RHI.
  - **KPI** : Baisse de 15%+ des `L1-dcache-load-misses` (via `just perf-benchmark`). Stabilité absolue des frame times (0 stutter remonté par `just benchmark-analyze`).
- **Bloc 2 : IBL & Logs (`vk_engine_ibl.cpp`, `app_log.cpp`)**
  - **Action** : Pré-allouer `std::vector<float>` pour le bake IBL dans la struct `IblResources` (réutilisation). Pré-allouer buffer `char[]` pour les logs.
  - **MVP** : Le bake IBL (traitement d'images) réutilise la mémoire au lieu de la recréer par slice/face.
  - **KPI** : Diminution drastique du count d'allocations dans l'onglet "Memory" du Tracy Profiler. `just verify-ibl` passe (pas de casse algorithmique).

______________________________________________________________________

## 4. Constantes Magiques

**Analyse** : Valeurs dures intra-code : `kGridSize = 10`, `kGridSpacing = 2.5f`, `#define MAX_SWAPCHAIN_IMAGES 8`. Floubes `0.5f` sans documentation dans l'initialisation des matériaux.
**Plan (Redesign)** :

- Extraire dans un header `engine_config.h` (ex: `struct RenderConfig`).
- Ajouter des suffixes typés et sémantiques : `constexpr float kGridSpacingMeters = 2.5f;`.
- Remplacer les macros C (`#define`) par des `constexpr size_t`.
  **Évaluations** :
- **Gains** : Maintenabilité et tweakabilité instantanées.
- **Risques** : Nul.
- **Coût** : Très faible (0.5 jour).
  **Tests & Profiling obligatoires** :
- `just sync-test-references-all` (Valide qu'aucune constante de rendu n'a été altérée silencieusement).

**Blocs d'Itérations (Branche `refactor/magic-constants`)** :

- **Bloc 1 : Extraction Core (`engine_config.h`)**
  - **Action** : Créer `src/engine_config.h`. Isoler `kGridSize`, `kGridSpacing` (-> `kGridSpacingMeters`), et les floats (0.5f, 1.0f) du `make_default_material()`.
  - **MVP** : Fichier config unique. Zéro constante magique dans `vk_engine_init.cpp`.
  - **KPI** : `just sync-test-references-all` valide que le rendu est strictement bit-exact à l'ancien. `just lint` valide le header.
- **Bloc 2 : Nettoyage Macros (`vk_engine.h`)**
  - **Action** : Remplacer `#define MAX_SWAPCHAIN_IMAGES 8` par `constexpr uint32_t kMaxSwapchainImages = 8;`. Retirer tout autre `#define` arbitraire.
  - **MVP** : Code C++ moderne (type safe) sans macros pré-processeur de config.
  - **KPI** : Compilation propre (aucun warning de typage), `just check-iter` au vert.

______________________________________________________________________

## 5. Qualité d'Architecture & Design (RHI vs Vulkan Natif)

**Analyse** : Une abstraction RHI existe (`rhi::`) mais `VulkanEngine` viole le contrat en manipulant `VkQueue`, `VkDevice` en dur. Couplage fort Frontend/Backend.
**Plan (Rearchitecture)** :

- Forcer l'abstraction totale : Aucun `#include <vulkan/vulkan.h>` autorisé dans la logique `VulkanEngine`.
- Passer par des interfaces de CommandList et de Queues (ex: `rhi->SubmitCommandList()`).
  **Évaluations** :
- **Gains** : Code ultra propre, switch API graphique possible, Mock RHI pour test unitaire pur.
- **Risques** : Perte de performance à cause de l'overhead d'abstraction (virtual calls, barriers non optimales). Cassures synchronisation.
- **Coût** : Très élevé (1-2 semaines).
  **Tests & Profiling obligatoires** :
- `just check-rhi-leaks` (Validation statique via Python script qu'aucun header backend ne fuit).
- `just verify-ibl` (Valide que la logique graphique de bake IBL abstraite reste conforme).
- `just benchmark-analyze` (Vérifier que l'overhead des appels virtuels RHI n'impacte pas le CPU time global par frame).
