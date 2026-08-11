# Plan Phase 2 : Architecture d'Upload HDR/IBL Multi-Hardware (Zéro Stutter)

**Date :** 2026-08-11
**Objectif KPI :** Éliminer le pic d'upload de 86 ms. Garantir 0 frames au-dessus de 16.6ms (60 FPS strict) lors d'un chargement HDR asynchrone, quelle que soit l'architecture (iGPU UMA ou dGPU dédié).

Ce plan propose une refonte ciblée du RHI Vulkan pour exploiter des "chemins d'optimisation" dynamiques selon la famille de GPU détectée.

______________________________________________________________________

## BLOC 1 : Détection Hardware & Files Dédiées (MVP Fondation) - **[TERMINE]**

**Concept :** Le RHI doit analyser le hardware au démarrage et ouvrir des files (Queues) spécialisées si elles existent.

- **Tâches :**
  1. Analyser `VkPhysicalDeviceMemoryProperties` (détection architecture UMA / Intel Iris).
  1. Analyser `VkQueueFamilyProperties` (recherche de files de Transfert et Compute sans bit Graphics pour NVIDIA/AMD).
  1. Activer des flags internes au RHI : `hasDedicatedTransferQueue`, `hasDedicatedComputeQueue`, `isUMA`.
- **KPI / Observable :** Logs au démarrage confirmant l'architecture exacte et les files matérielles allouées.

## BLOC 2 : Upload PCIe Zéro-Stutter (MVP Upload) - **[TERMINE]**

**Concept :** Détruire la frame de 83 ms passée dans `vkQueueSubmit_Transfer` en séparant la logique d'upload selon l'architecture.

- **Tâches :**
  - **Path A (iGPU UMA / Vulkan 1.4) :** Remplacer le Staging Buffer + `CmdCopy` par **Host Image Copy** (`vkCopyMemoryToImage`). Transfert direct RAM -> VRAM optimal sans passer par une Queue. (Note : Revert partiel vers Path B suite à des goulots CPU excessifs de 200ms sur UMA, validé par le profilage).
  - **Path B (dGPU NVIDIA/AMD) :** Soumettre le Staging Buffer sur la **Transfer Queue matérielle dédiée** en arrière-plan. Utiliser un Sémaphore pour signaler la fin à la Graphics Queue. Slicing du transfert DMA validé (chunks de 16 Mo).
- **KPI / Observable :** Benchmark Tracy `just benchmark-analyze`. Le pic de 80+ ms doit purement disparaître.

## BLOC 3 : Amortissement (Slicing) de la génération des Mipmaps HDR - **[TERMINE]**

**Concept :** Les 10 itérations de `CmdBlitImage` saturent la validation du driver si elles sont soumises d'un coup.

- **Tâches :**
  1. Étendre la State Machine IBL (`vk_engine_envmap.cpp`) avec de nouveaux états : `HdrMipmapGen`, `HdrMipmapGenWait`.
  1. Effectuer **1 seul Mipmap Blit par frame** (étalé sur 10 frames) au lieu de 10 d'un coup.
  1. Mettre à jour `Synchronization2` (`vkCmdPipelineBarrier2`) pour gérer les Layouts frame par frame.
- **KPI / Observable :** Lissage absolu du frametime post-copie PCIe (les micro-spikes de 2-3 ms disparaissent).

## 📌 ROADMAP : REPORTÉ (Faute de Hardware de Test)

### BLOC 4 : Compute Asynchrone vs Slicing Manuel (MVP Compute dGPU)

**Concept :** Déléguer le calcul IBL au matériel pour libérer complètement le CPU du micro-management (Slicing).
*(Note : Reporté car le GPU de développement actuel (Intel Iris Xe UMA) ne possède pas de Compute Queue dédiée, rendant ce code intestable. À reprendre sur un vrai poste de dev NVIDIA/AMD).*

- **Tâches futures :**
  - Conserver la State Machine de **Slicing CPU actuel** pour les iGPUs (Path A).
  - **Path B (dGPU / Compute Queue dédiée) :**
    - Soumettre tout le Bake IBL (Luminance, Irradiance, Prefilter, LUT) *d'un seul bloc* sur la Compute Queue asynchrone (Hardware Scheduler).
    - Utiliser un **Timeline Semaphore** pour signaler au Main Thread (sans Polling CPU actif) que l'environnement est cuit.
- **KPI Attendu :** Sur un GPU dédié, la charge CPU de l'IBL tombe à 0 ms absolu.
