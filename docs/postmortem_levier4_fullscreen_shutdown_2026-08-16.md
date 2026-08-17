# Post-Mortem & Analyse Technique : Levier 4, Fullscreen et Crashs au Shutdown

- **Date** : 16 Août 2026
- **Composants concernés** : `RenderGraph`, `Subpass Fusion`, `SwapchainManager`, `VulkanRHI`, `IblBaker`, `main.cpp`, CI Test Suite.
- **Statut** : Documentation & Analyse (aucun commit effectué).

______________________________________________________________________

## 1. Contexte et Problématique Initiale

Dans le cadre du **Levier 4 (Subpass Fusion / Input Attachments)**, les passes Forward et PostProcess ont été fusionnées en une unique `VkRenderPass` à deux sous-passes.
Cette optimisation a permis de supprimer le transit DRAM de l'attachement HDR intermédiaire (gain de 80% sur la saturation de bande passante DRAM).

Cependant, deux problèmes majeurs sont apparus lors de l'utilisation interactive :

1. **Régression Fullscreen (F11)** : L'affichage 3D restait confiné dans un rectangle $1024 \\times 768$ en haut à gauche de l'écran 1080p/1200p.
1. **Crash Signal 11 (SIGSEGV) à la fermeture de l'application** : `vulkan_app` plantait au moment du shutdown interactif (`Nettoyage et fermeture...`).

______________________________________________________________________

## 2. Analyse des Causes Racines

### A. Régression Fullscreen / Redimensionnement Stale

- **Cause** : Le Levier 1 a introduit la pré-enregistrement des Command Buffers secondaires Forward (`secondaryForwardCb`).
- **Mécanisme** : Lors du passage en plein écran (F11), `vk_recreate_swapchain` recréait la swapchain aux dimensions cibles ($1920 \\times 1200$) mais ne réinitialisait pas le booléen `secondaryForwardRecorded`. Les command buffers secondaires continuaient d'exécuter `vkCmdSetViewport` et `vkCmdSetScissor` avec les dimensions initiales ($1024 \\times 768$).
- **Fix apporté** :
  - Invalidation dynamique de `secondaryForwardRecorded` dès que `swapchainExtent` change dans `record_forward_pass`.
  - Forçage de `engine->secondaryForwardRecorded = false;` dans `vk_recreate_swapchain`.

### B. Crash Signal 11 au Shutdown (`vulkan_app`)

- **Cause** : Ordre de destruction asymétrique entre l'application principale (`src/main.cpp`) et le module RHI dynamique (`libvulkan_rhi.so`).
- **Mécanisme** :
  1. À la fermeture, `main.cpp` appelle `state.rhi->Shutdown()` -> `cleanup_vulkan_engine()` -> `cleanup_core_resources()` qui détruit le device Vulkan (`vkDestroyDevice(engine->ctx.device, nullptr)`).
  1. Ensuite, `main.cpp` appelle `destroyFunc(state.rhi)` -> `delete _engine` -> les destructeurs RAII C++ des pipelines (`~PipelinePtr`, `~DescriptorLayoutPtr`, etc.) s'exécutent.
  1. Ces destructeurs appelaient `DestroyPipeline`, `DestroyPipelineLayout`, `DestroyDescriptorLayout`, et `DestroyDescriptorPool` dans `src/rhi/vulkan_rhi.cpp`.
  1. Ces méthodes exécutaient par exemple `vkDestroyPipeline(_engine->ctx.device, handle, nullptr)` sans vérifier si le device était déjà détruit (`VK_NULL_HANDLE`).
  1. Le pilote graphique Vulkan (Mesa / Intel) segfaultait immédiatement (Signal 11 / Bad memory access) en recevant `VK_NULL_HANDLE` comme premier argument.
- **Fix apporté** : Ajout systématique de gardes `if (_engine && _engine->ctx.device != VK_NULL_HANDLE)` sur toutes les fonctions de destruction du `VulkanRHI`.

______________________________________________________________________

## 3. Pourquoi les Tests n'Avaient Pas Détecté ces Problèmes ?

| Type de Test | Ce qu'il testait | Angle Mort (Pourquoi le bug n'était pas vu) |
| :--- | :--- | :--- |
| **`SmokeTestApp`** | Exécution de `vulkan_app` pendant 2s via `scripts/smoke_test_app.sh`. | **Timeout SIGTERM** : Le script coupe le process avec `timeout 2s` sans lui laisser le temps d'exécuter son shutdown normal (`state.rhi->Shutdown()`). Le crash au shutdown n'était donc jamais atteint. |
| **`EngineIntegrationTest`** | Rendu de 6 frames fixes et comparaison d'images de référence. | **Résolution statique & Allocation pile** : Ne testait qu'une dimension fixe ($1024 \\times 768$) sans redimensionnement, et `VulkanEngine` était alloué sur la pile sans déchargement dynamique de module RHI `.so`. |
| **`LogicTests`** | Mathématiques et parsing hors contexte graphique Vulkan. | Aucun contexte GPU ou fenêtre. |

______________________________________________________________________

## 4. Mesures Prises pour Garantir la Non-Régression

1. **Ajout d'un test d'intégration de redimensionnement dynamique (`test_swapchain_resize`)** :
   - Teste explicitement la transition $1024 \\times 768 \\rightarrow 1280 \\times 720 \\rightarrow 1024 \\times 768$.
   - Vérifie la couverture effective des pixels aux coins de la fenêtre pour interdire tout clipping top-left.
1. **Blindage des méthodes RHI** :
   - Toutes les fonctions `Destroy*` sont désormais idempotentes et tolérantes aux destructions tardives ou partielles après destruction du device.
1. **Méthode `CleanupAllVmaResources()`** :
   - Nettoyage centralisé de toutes les allocations mémoires VMA gérées par le RHI avant `vmaDestroyAllocator`.

______________________________________________________________________

## 5. Bilan des Itérations et Consommation de Ressources

- **Nombre d'Itérations** : ~8 cycles complets de build/test/debug.
- **Outil Décisif** : **GDB en mode batch** (`gdb --batch -ex "run" -ex "bt" --args ./build/debug/unit_tests`) qui a immédiatement révélé la pile d'appels et l'appelant exact de la segfault en 1 invocation, là où CTest masquait les détails des signaux.
- **Validation Finale** :
  - `just check` : 100% Passed.
  - `just test` : 100% Passed (Release).
  - `just test-validation-layers` : 100% Passed (Debug + 0 Vulkan validation errors).
  - `just test-asan` : 100% Passed (0 leak, 0 undefined behavior).
