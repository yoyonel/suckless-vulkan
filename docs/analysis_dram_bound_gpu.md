# Post-Mortem Analysis : DRAM Bound & IBL Allocations

**Date** : 2026-08-13
**Sujet** : Réfutation de l'hypothèse des fuites mémoires CPU (`std::vector`) comme cause du DRAM Bound dans l'IBL.

## 1. Contexte & Hypothèse Initiale

Le profiling VTune remontait les métriques suivantes :

- **DRAM Bound** : ~10.9% (voire 14.4% de bande passante non-core)
- **LLC Misses** : 2.6M

L'hypothèse initiale (Bloc 2 du plan d'optimisation mémoire du 2026-08-12) imputait ce goulot d'étranglement à des allocations dynamiques massives (Heap) et des copies `std::vector` pendant le pre-processing IBL (notamment `floatData` et `finalData`).
Un commit précédent (`7fd2fb1`) avait tenté de résoudre ce problème en pré-allouant ces vecteurs en tant que membres de `IblBaker` (`m_exportFloatData`, `m_exportFinalData`).

## 2. Le Diagnostic Réel (Faux Positif)

Après un audit complet du chemin critique (hot path) et l'utilisation de `heaptrack`, il s'est avéré que l'hypothèse initiale était **fausse** :

1. **Chemin Froid (Cold Path)** : Les variables `m_exportFloatData` et `m_exportFinalData` ne sont utilisées *que* dans la fonction `save_image_as_hdr()`. Cette fonction n'est déclenchée qu'en "one-shot" par l'utilisateur (touche 'o') pour exporter les maps sur le disque.
1. **Absence dans le Benchmark** : Le benchmark automatisé (qui effectue des changements de maps HDR via `PageDown`) n'appelle **jamais** ce code. Les allocations de ces `std::vector` ne pouvaient donc pas être la cause des LLC Misses du rapport VTune.
1. **Zéro allocation IBL sur le Hot Path** : Le profiling `heaptrack` en situation réelle a confirmé que 100% des allocations massives (169k appels) sur le chemin de rendu proviennent du driver Vulkan Intel (`libvulkan_intel.so`) lors de `vkCmdBeginRenderPass` et de la soumission de frame, et non du code C++ de l'application.

## 3. La Vraie Nature du "DRAM Bandwidth Bound"

Puisque les allocations CPU ont été disculpées, d'où vient ce score élevé de `DRAM Bound` remonté par VTune (14.4% d'utilisation de bande passante `Uncore`) ?

La clé réside dans l'architecture matérielle : **l'Intel iGPU**.
Sur les puces graphiques intégrées, le CPU et le GPU **partagent la même mémoire physique (RAM)** et le même contrôleur mémoire.
Ce que VTune mesure comme un goulot "DRAM Bandwidth Bound", ce n'est pas le CPU qui sature la RAM via des `malloc()`, mais bien le **GPU** qui sature la bande passante de la RAM partagée lors du rendu de la frame.

Les coupables typiques côté GPU :

- Échantillonnage intensif des textures HDR non compressées.
- Transferts de grosses quantités de données brutes vers les SSBO/UBO.
- Mauvaise organisation des données provoquant des défauts de cache L1/L2 GPU (et donc des requêtes en RAM).

## 4. Les Limites de l'Architecture (Ce qui a déjà été tenté)

Pour éviter de futures régressions ou de fausses bonnes idées, il est crucial de documenter les limites atteintes :

### 4.1 L'Optimisation SSBO SoA (Déjà accomplie - Itération 10)

Il pourrait être tentant de vouloir optimiser les transferts mémoire du Host vers le Device. C'est **déjà fait**.
Lors de l'Itération 10 (Commit `5adf7c6`), le `TransformBuffer` a été basculé d'une architecture *AoS* massive (`mat4` de 64 octets) vers un *SoA partiel* (`vec4` positions uniquement de 16 octets, la matrice étant reconstruite par le GPU).
**Résultat** : Le trafic mémoire a été divisé par 4, le `L1 Bound` est passé de 12.4% à 8.4%, et les `LLC Misses` ont chuté de 25%. Il n'y a plus de gains logiciels évidents à tirer de la structuration des buffers Vulkan.

### 4.2 La Compression HDR Matérielle (BC6H / ASTC)

L'ultime piste pour réduire la bande passante consiste à utiliser des formats compressés matériellement (ex: BC6H) au lieu du Float 16-bits (`VK_FORMAT_R16G16B16A16_SFLOAT`), ce qui diviserait le poids des IBL par 8.
**La limitation (Trade-off)** :
Vulkan interdit de compresser dynamiquement vers du BC6H via un Compute Shader. Mettre en place cette solution impliquerait de **tuer la génération IBL en temps réel** pour basculer sur un pipeline de "baking offline" lourd (avec des outils CLI externes). Ce sacrifice de la flexibilité au profit de la mémoire a été jugé non pertinent pour le moment.

## 5. Conclusion Finale

L'optimisation des structures STL (`std::vector`) pour l'IBL était un chantier caduc (déjà hors du hot path). Le code C++ est désormais totalement purifié des allocations dynamiques nuisibles par frame (Validé par le chantier "Logs" - Point 1).

**Verdict** : Le `Memory Bound` oscille désormais autour de ~20% et le `DRAM Bandwidth Bound` autour de ~14%. Ces scores représentent le **mur matériel (Hardware Limit)** naturel d'un iGPU UMA fonctionnant à plein régime.
**Le Chantier 3 (Optimisation Mémoire) est officiellement clos.** Il n'est plus pertinent de chercher à réduire ces métriques par des optimisations C++ ou de la restructuration de bas niveau.
