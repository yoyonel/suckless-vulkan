# Bilan de Refactoring RHI - Handoff Context (2026-08-08)

## 1. État des lieux actuel (Jusqu'à la Phase 8.4)

Nous sommes en cours de refactoring massif pour abstraire les appels Vulkan directs de la logique métier (`vk_engine*`) vers une couche d'abstraction matérielle (`IRHI` / `VulkanRHI`).

**Phases réalisées :**

- **Phase 8.1 à 8.3 :** Migration des textures, des buffers, de la swapchain et du pipeline graphique vers des Handles (`TextureHandle`, `BufferHandle`).
- **Phase 8.4 (IBL) :** Migration des ressources liées à l'Image Based Lighting (Irradiance, Prefiltered, BRDF LUT) et des textures HDR. Tout est géré via des `TextureHandle` et `SamplerHandle`.

**Temps total investi (estimé) :** ~60 minutes effectives (itérations hachées en time-box de 3-5 minutes avec vérifications CI strictes).

## 2. Difficultés rencontrées et Dette Technique accumulée

1. **Dette d'abstraction (Leaky Abstraction) :**
   Les Descriptor Sets et les Pipelines n'étant pas encore abstraits (Phase 8.5), nous avons dû multiplier les casts affreux et les accès directs aux handles Vulkan sous-jacents (ex: `((VulkanRHI*)engine->rhi)->GetVkImageView(...)`). Cette fuite d'abstraction a gonflé la verbosité du code temporairement.
1. **Linter exigeant (`clang-tidy`) :**
   Le linter a une tolérance stricte sur la complexité cognitive (`threshold 25`). L'empilement des conditions pour créer les DescriptorSets nous a forcé à extraire la logique dans des helpers (`update_envmap_descriptor_set`).
1. **Le Segfault fatal (Signal 11) :**
   En voulant factoriser la mise à jour des bindings avec une boucle `for` magique, un décalage de binding a écrasé l'`Uniform Buffer` (binding 0) par un `Image Sampler`. Résultat : crash instantané du driver Intel au rendu de la Skybox. **Leçon apprise :** Bannir les boucles magiques pour les layouts asymétriques et privilégier l'affectation explicite unrollée. (Fixé : `writes[0].dstBinding = 1`, etc.).

## 3. Planification des phases restantes (Timebox 5 minutes)

L'objectif de la **Phase 8.5** est d'éliminer la dette technique (les casts `(VulkanRHI*)`) en abstraisant la tuyauterie restante de Vulkan. La **Phase 9** s'occupera d'encapsuler l'état global du moteur.

### **Phase 8.5 : Pipelines & Descriptor Sets (Estimation : 25 min)**

- **[8.5.1] Abstraction des Descriptor Pool/Layouts (5m) :**
  - Ajouter `IRHI::CreateDescriptorLayout()` et `IRHI::CreateDescriptorPool()`.
  - Remplacer les appels dans `vk_engine_init.cpp` et `vk_engine_ibl.cpp`.
- **[8.5.2] Abstraction des Descriptor Sets (5m) :**
  - Ajouter `IRHI::AllocateDescriptorSet()` et `IRHI::UpdateDescriptorSet()`.
  - Remplacer `vkAllocateDescriptorSets` et `vkUpdateDescriptorSets` (notamment notre fameux `update_envmap_descriptor_set`).
- **[8.5.3] Abstraction des Pipelines & PipelineLayouts (5m) :**
  - Ajouter des structures génériques (GraphicsPipelineDesc, ComputePipelineDesc) dans `IRHI`.
  - Ajouter `IRHI::CreateGraphicsPipeline()` et `IRHI::CreateComputePipeline()`.
- **[8.5.4] Abstraction des Commandes de Binding (5m) :**
  - Ajouter `IRHI::BindDescriptorSet()` et `IRHI::PushConstants()`.
  - Remplacer les `vkCmdBindDescriptorSets` et `vkCmdPushConstants` dans le code métier (`vk_ibl_bake`, `vk_draw_frame`).
- **[8.5.5] Nettoyage 8.5 (5m) :**
  - Supprimer toutes les utilisations de `((VulkanRHI*)engine->rhi)->GetVk...` dans la logique métier, la tuyauterie étant désormais purement RHI. Lancer un `just check`.

### **Phase 9 : État global de l'Engine (Estimation : 15 min)**

- **[9.1] Initialisation VMA et Device (5m) :**
  - Déplacer l'initialisation de `vmaAllocator`, `VkDevice`, et `VkInstance` dans `VulkanRHI::Init()`.
- **[9.2] Swapchain et RenderPass (5m) :**
  - Déplacer la gestion de la `VkSwapchainKHR` et de ses images dans le RHI.
  - Gérer la RenderPass par défaut dans le RHI.
- **[9.3] Nettoyage des includes (5m) :**
  - Objectif final : Supprimer `<vulkan/vulkan.h>` de `vk_engine.h`. L'Engine ne doit connaître que `IRHI`.

## 4. Contexte pour la reprise

- **Règle d'or :** 100% Caveman mode, rigueur absolue sur la qualité architecturale, pas de hardcoding magique, tester avec `just check` à la fin de chaque sous-phase de 5 minutes.
- Ne jamais commit/push sans validation du boss.
- Ne pas paniquer devant les erreurs `clang-tidy`, factoriser le code de façon propre avec des helpers si la complexité cognitive dépasse 25.
- L'application utilise Xdotool/GDB pour des tests d'interaction (ex: PageDown pour switcher le HDR), l'état du code est actuellement stable et compile à 100% sans warning, prêt à attaquer 8.5.1.
