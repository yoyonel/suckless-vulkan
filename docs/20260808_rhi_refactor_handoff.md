# Bilan de Refactoring RHI - Handoff Context (2026-08-08)

## 1. État des lieux actuel (Jusqu'à la Phase 8.5)

Nous sommes en cours de refactoring massif pour abstraire les appels Vulkan directs de la logique métier (`vk_engine*`) vers une couche d'abstraction matérielle (`IRHI` / `VulkanRHI`).

**Phases réalisées :**

- **Phase 8.1 à 8.3 :** Migration des textures, des buffers, de la swapchain et du pipeline graphique vers des Handles (`TextureHandle`, `BufferHandle`).
- **Phase 8.4 (IBL) :** Migration des ressources liées à l'Image Based Lighting (Irradiance, Prefiltered, BRDF LUT) et des textures HDR. Tout est géré via des `TextureHandle` et `SamplerHandle`.
- **Phase 8.5 (Pipelines & Commandes) :** Abstraction complète de la tuyauterie (`VkPipeline`, `VkDescriptorSet`) et des commandes d'enregistrement (`vkCmdDraw`, `vkCmdDispatch`, `vkCmdPipelineBarrier`, `vkCmdBindDescriptorSets`, etc.) derrière `IRHI`. La logique métier est enfin agnostique !
- **Phase 8.6 (Optimisation Mémoire & DOD - Itération 5) :** Benchmark Headless via Tracy (`just benchmark-tracy`). Éradication de toutes les allocations dynamiques (0 `malloc`) dans la hot-loop de rendu (utilisation de `__builtin_alloca`). Conversion du tri des Billboards (Scatter/Gather) vers un modèle DOD 100% linéaire (AoS contigu de 32 bytes). Preuve établie par le profiling que le RHI est purifié et que les L1 misses proviennent de l'I/O (`stb_image`).

**Temps total investi (estimé) :** ~110 minutes effectives (itérations hachées en time-box de 3-10 minutes avec vérifications CI strictes).

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

## 3. Planification des phases restantes (Timebox 5-10 minutes)

Le code métier étant désormais totalement isolé de Vulkan, l'objectif de la Phase 9 est de moduler l'architecture pour permettre le changement de backend à chaud (Hot-Reload) et la compilation en librairie dynamique.

### Phase 9 : RHI Backend Dynamique & Hot-Reload (Estimation : 100 min)

#### 9.1 Architecture DLL RHI (20 min)

- **Tâche 1 (10 min) :** Modifier `CMakeLists.txt`. Isoler `VulkanRHI` et `NullRHI` en cibles `SHARED`. Vérifier la compilation (`just build`).
- **Tâche 2 (10 min) :** Définir API C d'export (`extern "C" IRHI* CreateRHI()`, `DestroyRHI()`). Exposer symboles. Vérifier link (`just build`).

#### 9.2 Chargement Dynamique Runtime (10 min)

- **Itération 1 :** Implémenter classe `ModuleLoader` (wrappers OS). Refactor de l'initialisation du RHI dans le Core pour utiliser `dlopen`/`CreateRHI`. Vérification complète.

#### 9.3 Préservation d'État (EngineState) (20 min)

- **Itération 2 (10 min) :** Créer la structure globale `EngineState` (contexte non-graphique). Isoler et extraire l'état (inputs, caméra, etc.) hors du moteur graphique.
- **Itération 3 (10 min) :** Propager `EngineState` dans la boucle principale (`draw_frame`, `update`). Fixer toutes les erreurs de compilation et tests (`just test`).

#### 9.4 Logique Hot-Reload (10 min)

- **Itération 4 :** Implémenter la touche F5 pour le rechargement. Écrire la logique complète : Teardown du RHI existant, rechargement DLL, ré-instanciation et reconstruction des ressources via `EngineState`. Vérifier visuellement en modifiant une couleur à chaud.

## 4. Contexte pour la reprise

- **Règle d'or :** 100% Caveman mode, rigueur absolue sur la qualité architecturale, pas de hardcoding magique, tester avec `just check` à la fin de chaque sous-phase de 5 minutes.
- Ne jamais commit/push sans validation du boss. S'assurer de la propreté absolue de l'arbre git local.
- Séparation des préoccupations (SoC) stricte lors du découpage des commits.
- L'intégration de la CI/CD (GitHub Actions) est surveillée après chaque push. L'état actuel est fonctionnel et la CI a validé le dernier push.
