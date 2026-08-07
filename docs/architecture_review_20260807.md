# Revue Critique d'Architecture - Vulkan Engine (07 Août 2026)

## 1. Moteur de Rendu Mono-Trame (Single Frame in Flight)

- **Fichiers concernés** : `src/vk_engine_frame.cpp`, `src/vk_engine.h`
- **Finding** : L'engine ne possède qu'une seule `inFlightFence` et un seul jeu de sémaphores.
- **Problème** : Absence totale de recouvrement (overlap) entre le CPU et le GPU. Le CPU est bloqué (`vkWaitForFences`) à chaque frame en attendant que le GPU termine son rendu, et inversement.
- **Risques** :
  - Sous-utilisation sévère des ressources matérielles.
  - Plafond de performance artificiel (Frametime = Temps CPU + Temps GPU, au lieu du maximum des deux).
- **Corrections architecturales** :
  - Implémenter le principe de "Frames in Flight" via une stratégie de **Ring Buffers**.
  - Conceptuellement identique au [Double Buffering implémenté dans suckless-ogl via PBO](https://yoyonel.github.io/suckless-ogl/async_pbo/#1-double-buffering-unsynchronized-mapping), appliqué ici aux structures Vulkan : dupliquer Fences, Command Buffers, et Sémaphores dans un Ring Buffer (taille = 2 ou 3).
  - Validation requise par profiling (Tracy) pour consolider/prouver les benchmarks avant/après.
- **Gains** : Parallélisation CPU/GPU maximale, suppression des temps morts et hausse drastique du framerate.

______________________________________________________________________

## 2. Synchronisation Bloquante CPU/GPU (IBL Baking)

- **Fichiers concernés** : `src/vk_engine_ibl.cpp`
- **Finding** : La fonction `vk_ibl_bake` interrompt le flux de commandes GPU (`vkQueueWaitIdle(graphicsQueue)`) pour lire la luminance moyenne sur le CPU, avant de lancer la passe spéculaire.
- **Problème** : Bien qu'exécuté suite à un chargement asynchrone, le baking IBL est invoqué depuis le thread principal. L'attente `vkQueueWaitIdle` fige totalement le pipeline graphique (et donc le moteur) le temps du calcul.
- **Risques** :
  - Stuttering (micro-gels) massif lors du changement d'environnement.
  - Blocage complet de l'application sur des drivers lents (ex: +25s sur llvmpipe).
- **Corrections architecturales** :
  - Supprimer l'aller-retour CPU. Lier directement le buffer de luminance produit par la Passe 1 en tant que SSBO dans le shader de la Passe 2.
  - Déporter l'ensemble des commandes de génération IBL sur une file de calcul asynchrone (`computeQueue`) synchronisée avec la file graphique via des `Timeline Semaphores`.
- **Gains** : Zéro gel d'interface, génération des mipmaps en tâche de fond 100% sur le GPU.

______________________________________________________________________

## 3. Système d'Entrées par Polling Brut

- **Fichiers concernés** : `src/vk_engine_runtime.cpp`
- **Finding** : Les entrées utilisateur sont lues de manière synchrone via `glfwGetKey` directement dans la boucle de rendu.
- **Problème** : L'état des touches n'est vérifié qu'à l'instant `T` du rendu. Le système ne conserve aucun historique des événements survenus entre deux frames.
- **Risques** :
  - "Drop" d'inputs garanti si le frametime augmente (ex: lors d'un calcul lourd). Une frappe rapide pressée puis relâchée sera ignorée.
- **Corrections architecturales** :
  - Mettre en place un système événementiel via `glfwSetKeyCallback`.
  - Empiler les entrées interceptées dans une file d'événements (Event Queue) et traiter cette file au début de la logique CPU de la frame.
- **Gains** : Interface utilisateur réactive et 100% résiliente aux chutes de framerate, support fiable de l'automatisation de tests (Xdotool).
