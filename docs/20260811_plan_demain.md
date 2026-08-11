# Plan et Backlog pour la Session du 2026-08-11

## Bilan de la session précédente (Async HDR & IBL)

- **Statut** : Terminé & Validé.
- **Réalisations** :
  - Mise en place du chargement asynchrone des fichiers `.hdr` via un thread I/O lock-free.
  - Double-buffering des textures d'environnement (`pendingOldTextures`) pour éviter les glitchs visuels.
  - Découpage complet des Compute Shaders IBL (slicing ISO avec `suckless-ogl` : 12 slices Irradiance, 24 slices Prefilter Mip 0).
  - Correction finale du bug du pôle sud (truncation de division entière dans le dispatch de l'irradiance).
  - Automatisation du profiling via `just benchmark-tracy` et `just benchmark-analyze` (percentiles FPS).
- **Performances actuelles** : Le frame time pire cas est passé de ~282 ms à **86 ms**. Le reste de la construction IBL se fait de manière transparente à ~25-30 FPS.

## Prochaines Phases à Attaquer (Demain)

Voici les chantiers prioritaires à reprendre après la pause, basés sur les derniers retours et l'état du projet :

### 1. Refactor Global et Nettoyage (Code Review)

- **Contexte** : Le code du pipeline asynchrone IBL a été itéré très vite.
- **Action** : Revue de code globale. Restructuration de `vk_engine_envmap.cpp` et `vk_engine_ibl.cpp`, nettoyage des variables globales ou mal nommées, simplification de la machine d'état si possible.
- **Objectif** : Rendre la base de code robuste et maintenable avant d'ajouter plus de complexité.

### 2. Optimisation de l'Upload PCIe (La fameuse "Frame de 70-86 ms")

- **Contexte** : La génération des mipmaps est DÉJÀ time-slicée. La pire frame actuelle correspond en fait à **l'allocation mémoire synchrone sur le Main Thread (vkCreateImage / vmaCreateImage)** pour l'image 4K (134 Mo).
- **Action** : Déporter l'allocation mémoire VMA de `envHdrImage`, `irradianceMap`, et `prefilteredMap` intégralement dans le Thread I/O lock-free.
- **Objectif** : Viser le "Zéro Stutter" absolu (< 16 ms) lors des changements d'environnements.

### 3. Phase 5.4 : Memory False Sharing Padding

- **Contexte** : Sécurisation de l'architecture multithreadée.
- **Action** : Appliquer le padding (`alignas(128)`) sur les structures partagées entre le thread de rendu et le(s) thread(s) I/O pour éviter les invalidations de cache L1/L2 (False Sharing). *(À valider avec `perf c2c`)*.

### 4. Phase 5.2 : Système de Logs Synchrone Lock-Free

- **Contexte** : Le système de log actuel peut bloquer les threads critiques.
- **Action** : Implémenter un logger thread-safe sans mutex basé sur des buffers circulaires atomiques (Ring Buffer MPMC ou SPSC).
- **Objectif** : Ne plus jamais bloquer le thread de rendu à cause d'un `printf` ou d'un appel système d'I/O.

### 5. Intégration Tracy Profiler (Suite)

- Continuer d'instrumenter les zones d'ombre du code si nécessaire, en s'assurant que l'overhead reste minimal en build `tracy`.
