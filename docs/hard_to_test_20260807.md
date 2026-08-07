# Code difficile à tester (Hard to Test Code)

**Date :** 2026-08-07

Ce document liste les parties du code de Suckless-Vulkan qui sont délibérément non couvertes (ou partiellement couvertes) par les tests unitaires et d'intégration, ainsi que les raisons techniques expliquant cette difficulté.

## 1. Chargement asynchrone des ressources (Threads)

**Fichiers concernés :** `src/vk_engine_envmap.cpp` (ex: `hdr_io_thread_main`, `request_environment_texture_async`, `vk_process_ready_environment_texture`).

**Pourquoi c'est compliqué :**

- Le chargement des textures HDR se fait via un thread d'I/O en tâche de fond (`hdr_io_thread`) pour éviter de bloquer le thread principal.
- Le thread principal place une requête dans une file d'attente (queue) asynchrone et continue son exécution.
- Pour tester efficacement l'état de retour de la requête (réussite ou échec du chargement STB Image), le test d'intégration devrait utiliser des `sleep()` arbitraires ou des boucles d'attente actives pour synchroniser l'état.
- Introduire des timeouts arbitraires dans les tests CI rend les tests **flaky** (instables) car les temps de réponse dépendent de la charge CPU/disque de l'instance CI.
- **Bilan :** On teste uniquement le déclenchement de la requête et la configuration du thread. La logique de décodage asynchrone est laissée hors couverture pour garantir la rapidité et la fiabilité de la CI.

## 2. API Vulkan Natives et Appels VMA complexes

**Fichiers concernés :** (Divers modules Engine, ex: `vk_engine_init.cpp`, `vk_engine_ibl.cpp`)

**Pourquoi c'est compliqué :**

- Les appels profonds à la création d'images VMA (`vmaCreateImage`) ou de buffers avec mapping mémoire ne peuvent pas s'exécuter dans un environnement de tests unitaires (LogicTests) sans GPU.
- En tests d'intégration, forcer l'échec d'une allocation Vulkan (pour tester les branches d'erreur) est complexe : il faudrait injecter des mocks sur l'API Vulkan ou VMA elle-même (ex: forcer `vmaMapMemory` à retourner `VK_ERROR_OUT_OF_DEVICE_MEMORY`).
- **Bilan :** Les branches de nettoyage suite à un échec d'allocation (ex: libérer le staging buffer si `vmaCreateImage` échoue) restent non couvertes car ces conditions d'erreur sont presque impossibles à simuler proprement sans un framework de mock Vulkan lourd.

## 3. Pratiques de l'Industrie (Studios AAA)

Comment les moteurs professionnels (Unreal, Frostbite, Unity) résolvent ces problèmes de testabilité :

1. **Abstraction Vulkan (NullRHI) :** L'API graphique est derrière une interface (RHI). En mode test, un `NullRHI` est injecté. Le moteur tourne sans GPU, sans Vulkan. Les allocations (VMA) sont mockées.
1. **Job Systems (Task Graph) :** Fin des `std::thread` bruts. Tout est tâche. En tests, le *Job System* est basculé en **mode synchrone** : toutes les tâches asynchrones s'exécutent instantanément sur le thread principal. Le code devient 100% déterministe et testable sans `sleep`.
1. **Visual Regression Testing :** Plutôt que de viser la couverture de code sur Vulkan, les studios rendent l'image en *headless* (sans fenêtre) et comparent pixel par pixel avec une image de référence (*Golden Image*).
1. **Fermes matérielles (HITL) :** Les tests d'intégration profonds s'exécutent sur de véritables serveurs équipés de GPU (Hardware-in-the-loop), pas dans la CI unitaire rapide.
