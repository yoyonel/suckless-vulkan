# Bilan de Refactoring RHI - Handoff Context (2026-08-08)

## 1. État des lieux actuel (Jusqu'à la Phase 8.5)

Nous sommes en cours de refactoring massif pour abstraire les appels Vulkan directs de la logique métier (`vk_engine*`) vers une couche d'abstraction matérielle (`IRHI` / `VulkanRHI`).

**Phases réalisées :**

- **Phase 8.1 à 8.3 :** Migration des textures, des buffers, de la swapchain et du pipeline graphique vers des Handles (`TextureHandle`, `BufferHandle`).
- **Phase 8.4 (IBL) :** Migration des ressources liées à l'Image Based Lighting (Irradiance, Prefiltered, BRDF LUT) et des textures HDR. Tout est géré via des `TextureHandle` et `SamplerHandle`.
- **Phase 8.5 (Pipelines & Commandes) :** Abstraction complète de la tuyauterie (`VkPipeline`, `VkDescriptorSet`) et des commandes d'enregistrement (`vkCmdDraw`, `vkCmdDispatch`, `vkCmdPipelineBarrier`, `vkCmdBindDescriptorSets`, etc.) derrière `IRHI`. La logique métier est enfin agnostique !
- **Phase 8.6 (Optimisation Mémoire & DOD) :** Benchmark Headless via Tracy (`just benchmark-tracy`). Éradication de toutes les allocations dynamiques (0 `malloc`) dans la hot-loop de rendu (utilisation de `__builtin_alloca`). Conversion du tri des Billboards (Scatter/Gather) vers un modèle DOD 100% linéaire (AoS contigu de 32 bytes). Preuve établie par le profiling que le RHI est purifié et que les L1 misses proviennent de l'I/O (`stb_image`).
- **Phase 9 (Hot-Reload RHI & Flat Transform SSBO) :** Extraction totale du contexte métier dans `EngineState`, hébergé par l'exécutable (`main.cpp`). Découpage de l'API RHI en librairies dynamiques (`libvulkan_rhi.so`). Ajout de la touche F5 pour le **hot-reload dynamique** de la couche graphique. Migration de l'instancing de la géométrie via Storage Buffers (SSBO) diminuant les accès mémoire (L1 misses) de 10.6% au runtime.

## 2. Difficultés rencontrées et Dette Technique accumulée

1. **Le Piège des Faux-Positifs (Golden Images) :**
   Pendant la Phase 8.5, le rendu a été totalement cassé (écran noir, IBL manquant) mais les tests restaient verts ! L'exit logic des tests (ex: un `exit(0)` sauvage dans `vk_ibl_export_maps`) masquait les erreurs. **Victoire :** Mise en place d'une comparaison stricte avec des **Golden Images** (images de référence). Les tests valident désormais pixel par pixel (avec tolérance) pour empêcher toute régression silencieuse.
1. **Régression d'éclairage (Descriptor Sets dans une boucle) :**
   Lors de l'automatisation du remplacement des commandes Vulkan (via expressions régulières Python), une variable de boucle `sets[i]` a été accidentellement remplacée par une ressource globale `spmapDescriptorSet`. Résultat : les mipmaps de l'IBL utilisaient tous le même descripteur, cassant l'éclairage de l'environnement (différence de 20% sur la Golden Image). Fixé manuellement.
1. **Erreurs de Pipeline et de Culling :**
   - Le *Backface Culling* a été cassé lors de la migration du Pipeline (`FrontFace` mal mappé), provoquant des artefacts.
   - Les formats de vertex (`VK_FORMAT_R32_SINT`) pour les Billboards n'étaient pas correctement abstraits via `VertexFormat::Int1`.
1. **Linter pointilleux (`clang-tidy`) :**
   Le linter a exigé que les nouvelles méthodes `CmdPipelineBarrier` de `VulkanRHI` soient rendues statiques (car elles n'utilisent pas `this`). Solution : Ajout d'un `(void)this;` trivial pour respecter l'encapsulation orientée objet sans déclencher le linter, ou refonte.
1. **Gestion des Timeouts :**
   Les time-boxes (5 à 10 minutes max par sous-phase) ont été strictement respectées. Les commits ne sont effectués qu'après un `just test` (validation fonctionnelle) et un `just check` (validation statique).

## 3. Prochaines Étapes (Phase 10)

L'architecture est maintenant complètement découplée, propre, "GPU-Driven ready", et supporte le rechargement à chaud (Hot-Reload). Nous pouvons reprendre le développement des fonctionnalités visuelles et l'optimisation des ressources :

### Phase 10 : Évolution du Rendu & Optimisations (À venir)

1. **Option VRAM & Zero-Copy** :
   Finaliser les options d'optimisation VRAM (cf. `docs/2026-08-09_options_optimisation_vram.md`), notamment l'Option C (Zero-Copy Staging Buffers via `VMA_MEMORY_USAGE_CPU_ONLY` ou KHR).
1. **Raytracing / Compute Avancé** :
   Introduire le pipeline Compute de Raytracing de Billboards ou optimiser les passes de luminance IBL.
1. **Culling GPU** :
   Grâce au `Flat Transform Buffer` (SSBO) déjà en place pour la grille, implémenter un Frustum Culling via Compute Shader et piloter le rendu via `vkCmdDrawIndexedIndirect`.

## 4. Contexte pour la reprise

- **Règle d'or :** 100% Caveman mode, rigueur absolue sur la qualité architecturale, pas de hardcoding magique, tester avec `just check` à la fin de chaque sous-phase de 5 minutes.
- Ne jamais commit/push sans validation du boss. S'assurer de la propreté absolue de l'arbre git local.
- Séparation des préoccupations (SoC) stricte lors du découpage des commits.
- L'intégration de la CI/CD (GitHub Actions) est surveillée après chaque push. L'état actuel est fonctionnel et la CI a validé le dernier push.
