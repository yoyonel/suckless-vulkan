# Focus : Job Systems (Task Graphs)

**Date :** 2026-08-07

## Le Concept

Dans les moteurs de jeux modernes (AAA), l'utilisation directe de `std::thread` et `std::mutex` est proscrite. On découpe le travail en petites fonctions (les *Jobs* ou *Tasks*) liées par un graphe de dépendance (ex: *Calculer Physique* DOIT terminer avant *Update Camera*).

Le moteur possède un **Task Scheduler** (Planificateur) et un **Thread Pool** fixe (souvent calibré sur le nombre de cœurs physiques du CPU, par ex. 8 threads pour un 8 cœurs). Le scheduler distribue les jobs libres aux threads disponibles sans aucun temps mort.

### Avantages

1. **Zéro Oversubscription :** Pas de création incontrôlée de dizaines de threads qui s'étouffent entre eux (context switching). L'utilisation CPU est maximisée.
1. **Lock-Free :** On remplace les mutex (bloquants) par les dépendances de jobs au sein du graphe.
1. **Tests Unitaires Déterministes (Le Saint-Graal) :** En intégration continue (CI), on peut instancier le Scheduler avec `ThreadCount = 1` (ou dans un mode explicitement synchrone). TOUT s'exécute alors séquentiellement de manière 100% prédictive. Zéro flaky tests, débug garanti.

### Inconvénients

1. **Intrusif :** Oblige à tout coder sous forme de lambdas ou de classes encapsulant des tâches. Le code perd sa linéarité naturelle.
1. **Débug Complexe :** En cas de crash dans un job, la *callstack* (pile d'appel) indique l'exécuteur générique (ex: `WorkerThread::Run()`), perdant ainsi le contexte de la fonction d'origine ayant posté le job.

## Références Industrielles (GDC)

- **Bungie (GDC 2015)** : *Destiny's Multithreaded Rendering Architecture*. [Regarder sur GDC Vault](https://gdcvault.com/play/1022186/Destiny-s-Multithreaded-Rendering). Explique la transition d'un modèle thread-based à une architecture en phases/jobs purs.
- **Naughty Dog (GDC 2015)** : *Parallelizing the Naughty Dog Engine Using Fibers*. [Regarder sur GDC Vault](https://www.gdcvault.com/play/1022186/Parallelizing-the-Naughty-Dog-Engine). Détaille l'utilisation des *Fibers* (threads très légers natifs à l'OS) pour suspendre et reprendre des jobs en quelques microsecondes sans bloquer le thread physique.

______________________________________________________________________

## C++ Taskflow (L'outil recommandé)

Pour implémenter ce modèle d'architecture en C++ moderne sans réinventer la roue, la bibliothèque de référence open-source est **Taskflow**.

- **Dépôt GitHub :** [https://github.com/taskflow/taskflow](https://github.com/taskflow/taskflow)
- **Documentation :** [https://taskflow.github.io/](https://taskflow.github.io/)

### Qu'est-ce que Taskflow ?

Taskflow est une bibliothèque C++ *header-only* (ou intégrable via CMake) ultra-rapide permettant de construire des graphes de tâches parallèles. Son *overhead* de scheduling est minuscule (entre 5 et 50 nanosecondes), ce qui le rend adapté pour la boucle de rendu d'un jeu vidéo à 60 ou 120 FPS.

### Comment l'utiliser ?

Au lieu d'écrire des threads manuellement, on déclare des tâches (`tf::Task`) et on les relie.

```cpp
#include <taskflow/taskflow.hpp>

tf::Executor executor; // Pool de threads (automatiquement calibré au CPU)
tf::Taskflow taskflow; // Le graphe

auto taskA = taskflow.emplace([] () { std::cout << "Input/Physique\n"; });
auto taskB = taskflow.emplace([] () { std::cout << "Update Logic\n"; });
auto taskC = taskflow.emplace([] () { std::cout << "Render Setup\n"; });

// Dépendances : B s'exécute après A. C s'exécute après B.
taskA.precede(taskB); 
taskB.precede(taskC);

// Exécution asynchrone (non bloquante)
executor.run(taskflow).wait();
```

### Transition pour Suckless-Vulkan

Le coût de transition vers ce modèle est **Modéré à Élevé**.
Il faudrait casser l'actuelle boucle monolithique `while(!glfwWindowShouldClose)` en jobs discrets (`Job_Input`, `Job_Logic`, `Job_RenderSetup`, `Job_RenderSubmit`).

- **Durée estimée :** 2 à 4 semaines de refactoring profond.
- **Bénéfice :** L'architecture ferait un saut technique majeur, passant d'un moteur amateur à un design de moteur pro, tout en réglant définitivement les problématiques de flaky tests (comme le chargement HDR).
