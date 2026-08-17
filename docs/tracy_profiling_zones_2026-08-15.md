# Cartographie des Zones & Pistes de Profiling Tracy (15 Août 2026)

## 1. Contexte & Architecture du Profiling

Le moteur `suckless-vulkan` intègre une instrumentation de profiling temps-réel basée sur **Tracy Profiler** (`TRACY_ENABLE`, `TRACY_FIBERS`). L'architecture s'articule autour de 4 dimensions :

1. **Timeline GPU Vulkan** (`SVK_TRACY_VK_ZONE_C` / `TracyVkZone`) : Mesure matérielle nanoseconde via requêtes de timestamps GPU calibrées sur la queue graphique.
1. **Timeline CPU Standardisée** (`SVK_TRACY_ZONE_SCOPED_C` / `SVK_TRACY_ZONE_NAMED_C`) : Mesure des phases logiques et d'enregistrement du `Main thread` et du `HDR I/O Thread`.
1. **Pistes Virtuelles (Fibers)** (`SVK_TRACY_FIBER_ZONE_C`) : Visualisation synthétique sous forme de timeline dédiée des machines à états asynchrones (`Async Status`) et des ratios de coordination CPU/GPU (`Hybrid Perf`).
1. **Suivi Mémoire & Allocations** (`SVK_TRACY_ALLOC` / `SVK_TRACY_FREE`) : Analyse des arènes scratch et des buffers intermédiaires STB/KTX2.

______________________________________________________________________

## 2. Pistes Virtuelles (Fibers Tracy)

Les pistes virtuelles permettent d'agréger des états asynchrones multi-threads ou d'isoler des profils hybrides CPU-GPU au sein de pistes dédiées indépendantes de la granularité des threads OS.

### 2.1. Piste Virtuelle : `Hybrid Perf` (Performance Hybride CPU / GPU)

La piste **`Hybrid Perf`** permet de diagnostiquer l'efficacité de la coordination hôte/device lors des calculs compute lourds (Bake IBL : Luminance, BRDF LUT, Irradiance, Prefilter spéculaire).

| Zone / Label Tracy | Couleur | Emplacement Source | Déclencheur (Trigger) | Rôle & Intérêt Diagnostique |
|---|---|---|---|---|
| **`Host (CPU): Luminance`** | 🟦 `#1E88E5` (Bleu azur) | \[`src/vk_engine_ibl.cpp`\](file:///home/latty/Prog/__PERSO__/suckless-vulkan/src/vk_engine_ibl.cpp) | `vk_ibl_bake_luminance` | **Préparation compute Luminance** : Configuration des descripteurs et dispatch de réduction parallèle. |
| **`Host (CPU): BRDF LUT`** | 🟦 `#1E88E5` (Bleu azur) | \[`src/vk_engine_ibl.cpp`\](file:///home/latty/Prog/__PERSO__/suckless-vulkan/src/vk_engine_ibl.cpp) | `vk_ibl_bake_brdf` | **Préparation compute BRDF LUT** : Configuration des descripteurs et dispatch 512x512. |
| **`Host (CPU): Irradiance`** | 🟦 `#1E88E5` (Bleu azur) | \[`src/vk_engine_ibl.cpp`\](file:///home/latty/Prog/__PERSO__/suckless-vulkan/src/vk_engine_ibl.cpp) | `vk_ibl_bake_irradiance` | **Préparation compute Irradiance** : Ordonnancement d'une tranche de convolution diffuse. |
| **`Host (CPU): Specular`** | 🟦 `#1E88E5` (Bleu azur) | \[`src/vk_engine_ibl.cpp`\](file:///home/latty/Prog/__PERSO__/suckless-vulkan/src/vk_engine_ibl.cpp) | `vk_ibl_bake_prefilter` | **Préparation compute Spéculaire** : Ordonnancement d'un niveau de rugosité GGX. |
| **`Sync (GPU Wait)`** | 🟪 `#8E24AA` (Violet profond) | \[`src/vk_engine_envmap.cpp`\](file:///home/latty/Prog/__PERSO__/suckless-vulkan/src/vk_engine_envmap.cpp), \[`src/vk_engine_ibl.cpp`\](file:///home/latty/Prog/__PERSO__/suckless-vulkan/src/vk_engine_ibl.cpp) | `vk_check_ibl_bake_status` (polling fence) ou `end_single_time_commands` (`vkQueueWaitIdle`) | **Temps d'attente / Synchronisation hôte** : Mesure le temps CPU consommé à sonder l'état de complétion du GPU (`vkGetFenceStatus`) ou à bloquer l'hôte (`vkQueueWaitIdle`). Révèle les bulles de synchronisation CPU-GPU. |

> [!NOTE]
> **Interprétation visuelle dans Tracy** : En observant simultanément la piste `Hybrid Perf` et la piste `Vulkan Graphics Queue`, vous pouvez comparer le temps de préparation CPU (`Host (CPU)`), le temps de calcul matériel GPU (`GPU IBL Specular`, etc.) et le temps de vérification/attente (`Sync (GPU Wait)`).

______________________________________________________________________

### 2.2. Piste Virtuelle : `Async Status` (Cycle de Vie du Chargeur HDR)

La piste **`Async Status`** retrace la machine à états complète du chargement asynchrone d'environnement HDR / Skybox.

| Zone / Label Tracy | Couleur | Emplacement Source | Déclencheur (Trigger) | Rôle & Intérêt Diagnostique |
|---|---|---|---|---|
| **`Async IDLE`** | ⬜ `#9E9E9E` (Gris neutre) | \[`src/tracy_state.cpp`\](file:///home/latty/Prog/__PERSO__/suckless-vulkan/src/tracy_state.cpp) | `set_async_status(Idle)` | **État inactif** : Le thread worker est en attente d'une requête de chargement dans la SPSC queue. Zone continue ininterrompue gérée de façon stateful pour éviter tout micro-découpage répétitif. |
| **`Async PENDING`** | 🟨 `#FDD835` (Jaune vif) | \[`src/vk_engine_envmap.cpp`\](file:///home/latty/Prog/__PERSO__/suckless-vulkan/src/vk_engine_envmap.cpp) | `request_environment_texture_async` | **Requête soumise** : L'utilisateur a demandé un changement de skybox (clavier/UI) ; la requête est empilée. |
| **`Async LOADING`** | 🟩 `#43A047` (Vert herbe) | \[`src/vk_engine_envmap.cpp`\](file:///home/latty/Prog/__PERSO__/suckless-vulkan/src/vk_engine_envmap.cpp) | `process_one_hdr_load_request` | **Prise en charge** : Le thread worker dépile la requête et commence la résolution des chemins d'accès disque. |
| **`Async CONVERT`** | 🟦 `#00ACC1` (Cyan) | \[`src/vk_engine_envmap.cpp`\](file:///home/latty/Prog/__PERSO__/suckless-vulkan/src/vk_engine_envmap.cpp) | `load_hdr_with_ktx2_cache` | **Décodage & Transcodage** : Parsing disque du fichier `.hdr` / `.ktx2`, décompression des pixels float32/float16. |
| **`Async READY`** | 🍏 `#7CB342` (Vert clair pomme) | \[`src/vk_engine_envmap.cpp`\](file:///home/latty/Prog/__PERSO__/suckless-vulkan/src/vk_engine_envmap.cpp) | `allocate_hdr_resources_async`, `vk_process_ready_environment_texture` | **Prêt & Injection VRAM** : Mémoire staging VMA allouée côté worker, puis bascule et upload GPU par le Main thread. |
| **`Async FAILED`** | 🟥 `#E53935` (Rouge écarlate) | \[`src/vk_engine_envmap.cpp`\](file:///home/latty/Prog/__PERSO__/suckless-vulkan/src/vk_engine_envmap.cpp) | Erreur disque ou format invalide | **Échec** : Fichier corrompu ou introuvable ; passage en mode fallback. |

______________________________________________________________________

## 3. Timeline Vulkan GPU (Matériel)

Instrumenté via `SVK_TRACY_VK_ZONE_C` sur le contexte `TracyVkContextHostCalibrated`.

| Zone GPU | Couleur | Piste Tracy | Passe / Shader | Rôle & Intérêt Diagnostique |
|---|---|---|---|---|
| **`Forward Pass`** | 🟪 `#7E57C2` (Violet pastel) | Vulkan Graphics Queue | RenderPass Principale | Englobe l'ensemble des commandes graphiques de la géométrie et du skybox. |
| **`GPU Geometry`** | 🟧 `#FB8C00` (Orange ambre) | Vulkan Graphics Queue | `shaders/shader.vert` / `.frag` | Rendu 3D maillages/objets avec éclairage PBR direct + ombrage. |
| **`GPU Skybox`** | 🟦 `#0288D1` (Bleu ciel) | Vulkan Graphics Queue | `shaders/skybox.vert` / `.frag` | Rendu du dôme d'arrière-plan avec cubemap d'environnement. |
| **`PostProcess Pass`** | 🟪 `#8E24AA` (Pourpre) | Vulkan Graphics Queue | `shaders/postprocess.frag` | Tone-mapping (ACES/Reinhard), correction gamma et composition finale. |
| **`GPU IBL Luminance`** | 🟨 `#FFB300` (Or compute) | Vulkan Graphics Queue | `shaders/ibl_lum_pass1.comp` / `pass2` | Réduction parallèle compute pour estimer la luminance moyenne de la skybox. |
| **`GPU IBL BRDF LUT`** | 🟦 `#039BE5` (Bleu compute) | Vulkan Graphics Queue | `shaders/ibl_spbrdf.comp` | Génération de la table 2D Split-Sum BRDF LUT (512x512). |
| **`GPU IBL Irradiance Slice`** | 🟧 `#F57C00` (Orange chaud) | Vulkan Graphics Queue | `shaders/ibl_irmap.comp` | Convolution hémisphérique diffuse par tranche de cubemap. |
| **`GPU IBL Specular`** | 🟥 `#D81B60` (Magenta compute) | Vulkan Graphics Queue | `shaders/ibl_spmap.comp` | Filtrage spéculaire GGX par niveau de rugosité (roughness mips). |

______________________________________________________________________

## 4. Timeline CPU Standardisée

Instrumenté via `SVK_TRACY_ZONE_SCOPED_C` et `SVK_TRACY_ZONE_NAMED_C`.

### 4.1. Boucle Principale de Rendu (`Main thread`)

Fichier source : \[`src/vk_engine_frame.cpp`\](file:///home/latty/Prog/__PERSO__/suckless-vulkan/src/vk_engine_frame.cpp)

| Zone Standardisée | Couleur | Fréquence | Rôle & Intérêt Diagnostique |
|---|---|---|---|
| **`Total Frame`** | 🟩 `#2E7D32` (Vert émeraude) | 1 / frame | **Parente de la Frame CPU** : Mesure la durée CPU totale de production d'une image. |
| **`Frame Acquire Swapchain`** | 🟧 `#F57C00` (Orange vif) | 1 / frame | Attente `vkWaitForFences` et acquisition de l'image de swapchain (`vkAcquireNextImageKHR`). Révèle les blocages en amont (GPU bound). |
| **`Frame Scene Update`** | 🟦 `#1976D2` (Bleu cobalt) | 1 / frame | Mise à jour logique : caméra, inputs clavier/souris, physique, uniforms MVP. |
| **`RenderGraph Execute & Record`** | 🟪 `#7B1FA2` (Violet royal) | 1 / frame | Compilation du DAG RenderGraph, calcul des barrières de synchronisation Vulkan et enregistrement des passes dans le command buffer. |
| **`Frame Queue Submit & Present`** | 🟦 `#00897B` (Turquoise foncé) | 1 / frame | Soumission (`vkQueueSubmit`) et présentation écran (`vkQueuePresentKHR`). **C'est ici qu'apparaît l'attente VSync**. |
| **`Frame Process Ready Texture`** | 🟦 `#42A5F5` (Bleu ciel clair) | 1 / frame | Dépilement de la texture HDR décodée, injection dans le descripteur global et démarrage du bake IBL. |

______________________________________________________________________

### 4.2. Sous-Système HDR & I/O (`HDR I/O Thread`)

Fichier source : \[`src/vk_engine_envmap.cpp`\](file:///home/latty/Prog/__PERSO__/suckless-vulkan/src/vk_engine_envmap.cpp)

| Zone Standardisée | Couleur | Fréquence | Rôle & Intérêt Diagnostique |
|---|---|---|---|
| **`Async Loader: Process Request`** | 🟫 `#795548` (Brun chaud) | Ponctuelle | Prise en charge d'une requête de chargement HDR sur le worker thread. |
| **`Async Loader: Decode File`** | 🟫 `#8D6E63` (Brun clair) | Ponctuelle | Décodage disque et parsing CPU de l'image HDR/KTX2. |
| **`Async Loader: VMA Allocations`** | 🟫 `#A1887F` (Brun pâle) | Ponctuelle | Allocations mémoires VMA (buffer staging hôte et image cubemap device). |
| **`Async Loader: Staging Upload`** | 🟫 `#6D4C41` (Brun moyen) | Ponctuelle | Téléversement des données de pixels vers la mémoire intermédiaire staging. |
| **`Async Loader: Bake Allocations`** | 🟫 `#5D4037` (Brun sombre) | Ponctuelle | Création des buffers et images nécessaires aux passes de calcul IBL. |
| **`Async Loader: Request Envmap`** | 🟦 `#1976D2` (Bleu cobalt) | Ponctuelle | Soumission de la demande de chargement dans la SPSC queue côté Main thread. |
| **`Async Loader: Cleanup`** | ⬜ `#78909C` (Gris bleuâtre) | Ponctuelle | Destruction asynchrone des anciens objets et buffers VMA devenus orphelins. |
| **`Async Loader: Mipmap Slice`** | 🟫 `#8D6E63` (Brun clair) | Ponctuelle | Génération de la chaîne de mipmaps pour une tranche d'environnement. |

______________________________________________________________________

### 4.3. Passes de Calcul IBL (`Main thread`)

Fichier source : \[`src/vk_engine_ibl.cpp`\](file:///home/latty/Prog/__PERSO__/suckless-vulkan/src/vk_engine_ibl.cpp)

| Zone Standardisée | Couleur | Fréquence | Rôle & Intérêt Diagnostique |
|---|---|---|---|
| **`IBL Bake: Luminance Pass`** | 🟨 `#FFB300` (Or compute) | Ponctuelle | Ordonnancement de la passe de calcul de luminance moyenne. |
| **`IBL Bake: BRDF LUT Pass`** | 🟦 `#039BE5` (Bleu compute) | Unique (startup) | Ordonnancement de la génération de la LUT BRDF Split-Sum. |
| **`IBL Bake: Irradiance Pass`** | 🟧 `#F57C00` (Orange chaud) | 12 tranches / bake | Ordonnancement de la passe de convolution diffuse par tranches temporelles. |
| **`IBL Bake: Specular Prefilter Pass`** | 🟥 `#D81B60` (Magenta compute) | Mips / bake | Ordonnancement du filtrage spéculaire GGX multi-niveaux de rugosité. |

______________________________________________________________________

### 4.4. Cycle de Vie du Moteur

Fichier source : \[`src/vk_engine_init.cpp`\](file:///home/latty/Prog/__PERSO__/suckless-vulkan/src/vk_engine_init.cpp)

| Zone Standardisée | Couleur | Fréquence | Rôle & Intérêt Diagnostique |
|---|---|---|---|
| **`Engine: Initialize Vulkan`** | ⬛ `#263238` (Bleu nuit) | Unique (démarrage) | Temps de démarrage global : instance, device, swapchain, shaders, render graph, pipelines, assets de base. |
| **`Engine: Shutdown Vulkan`** | ⬛ `#263238` (Bleu nuit) | Unique (fermeture) | Temps d'extinction propre : purge des queues, destruction ordonnée des ressources RHI et désallocation VRAM. |

> [!NOTE]
> **Persistance des Métadonnées en Module Partagé (`RTLD_NODELETE`)** :
> `libvulkan_rhi.so` étant chargé via `dlopen`, le flag `RTLD_NODELETE` est actif sous `TRACY_ENABLE` (\[`src/module_loader.cpp`\](file:///home/latty/Prog/__PERSO__/suckless-vulkan/src/module_loader.cpp)). Cela empêche `dlclose()` de démapper la mémoire `.rodata` avant que le client Tracy n'ait transmis les chaînes de caractères de `Engine: Shutdown Vulkan` au serveur/trace (évitant le fallback `???` dans Tracy).

______________________________________________________________________

## 5. Suivi Mémoire & Allocations (Memory Profiling)

| Macro / Mécanisme | Emplacement Source | Événement Déclencheur (Trigger) | Rôle & Intérêt Diagnostique |
|---|---|---|---|
| **`SVK_TRACY_ALLOC(arena, cap)`** | \[`src/core_engine.cpp`\](file:///home/latty/Prog/__PERSO__/suckless-vulkan/src/core_engine.cpp) | Création d'une `LinearArena` / `ScratchBuffer` | Trace les allocations volumineuses en mémoire CPU gérées par les allocateurs internes. |
| **`SVK_TRACY_FREE(arena)`** | \[`src/core_engine.cpp`\](file:///home/latty/Prog/__PERSO__/suckless-vulkan/src/core_engine.cpp) | Destruction d'une arène | Vérifie l'absence de fuite mémoire sur les buffers scratch. |
| **`SVK_TRACY_ALLOC(stb_ptr, sz)`** | \[`src/stb_image_impl.cpp`\](file:///home/latty/Prog/__PERSO__/suckless-vulkan/src/stb_image_impl.cpp) | Décodage d'un fichier image `.png` / `.hdr` via STB | Affiche dans le graphique **Memory Usage** de Tracy les pics d'allocations temporaires liés au déballage des textures. |
| **`SVK_TRACY_FREE(stb_ptr)`** | \[`src/stb_image_impl.cpp`\](file:///home/latty/Prog/__PERSO__/suckless-vulkan/src/stb_image_impl.cpp) | Fin de transfert GPU d'une texture STB | Confirme la désallocation immédiate de la mémoire RAM après téléversement VRAM. |

______________________________________________________________________

## 6. Outils de Vérification Automatisée

Le dépôt intègre une suite de validation automatique pour certifier la conformité de la timeline et des pistes virtuelles :

```bash
# Exécution du benchmark et validation des invariants GPU/CPU/Fibers
just benchmark-tracy

# Exécution automatisée headless en environnement CI (Xvfb)
just test-integration-tracy

# Vérification manuelle d'une trace capturée
just verify-tracy-trace build/tracy/benchmark.tracy
```
