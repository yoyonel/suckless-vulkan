# Backlog Chantiers - 2026-08-09 23:07

Ce document liste les chantiers restants (hors Render Graph) suite au refactoring RHI, avec leurs objectifs, risques et stratégies de validation.

## 1. Intégration Tracy Profiler (Bug Multi-Contexte)

### Objectifs & Avantages (Tracy)

- **Profilage Précis :** Actuellement Tracy fonctionne mal ou pas du tout avec de multiples contextes (CPU / GPU Vulkan). Le but est d'avoir une timeline fluide mixant CPU frames et GPU zones.
- **Performance :** Identifier rapidement les goulets d'étranglement CPU (soumission) et GPU (rendu).

### Risques Encourus (Tracy)

- **Deadlocks / Crashs :** L'instrumentation de threads et de files d'attente GPU peut causer des problèmes de synchronisation.
- **Overhead :** Si mal configuré, le profiler peut ralentir drastiquement l'application, faussant les mesures.

### Stratégie de Validation (Tracy)

- **Tests Unitaires :** Mock des macros Tracy pour s'assurer qu'elles compilent sans Tracy activé.
- **Tests Fonctionnels :** Build avec `TRACY_ENABLE=ON`. Connexion du client Tracy externe et vérification de la réception des frames.
- **Benchmarks :** Comparer le frame time moyen avec et sans Tracy pour mesurer l'overhead (cible : < 5%).

______________________________________________________________________

## 2. ~~Refactoring `main.cpp` (Complexité Cognitive)~~ [FAIT]

### Objectifs & Avantages (main.cpp)

- **Lisibilité :** `main.cpp` gère actuellement trop de choses (initialisation fenêtre, boucle principale, inputs, rechargement de modules). L'objectif est d'extraire la logique dans une classe `App` ou des sous-systèmes dédiés.
- **Maintenabilité :** Rendre l'ajout de nouvelles fonctionnalités (nouvelles scènes, nouveaux inputs) trivial sans toucher au point d'entrée.

### Risques Encourus (main.cpp)

- **Régression Globale :** Toucher à la boucle principale risque de casser la gestion des événements OS (clavier/souris) ou le cycle de vie de la fenêtre.
- **Pertes de Performance :** L'ajout de nouvelles couches d'abstraction peut introduire de l'overhead (pointeurs virtuels, dispatch dynamique) dans la boucle critique.

### Stratégie de Validation (main.cpp)

- **Tests Unitaires :** Isoler les gestionnaires d'inputs et la boucle d'application via injection de dépendances.
- **Tests d'Intégration :** Lancer `LogicTests` et `SmokeTestApp`.
- **Benchmark :** Mesurer le L1/L2 Cache Miss de la boucle principale avec `perf stat` avant/après refacto pour assurer aucune régression.

______________________________________________________________________

## 3. Refonte de la CI/CD (ISO Local/CI) [TODO]

### Objectifs & Avantages (CI/CD)

- **Fiabilité Absolue :** Actuellement, la CI masque certaines erreurs (notamment les memory leaks ASan qui retournent un code 0 via `scripts/ci/run_ci_asan.sh`). L'objectif est d'avoir une CI 100% ISO avec l'environnement local.
- **Fail-Fast :** S'assurer que toute régression ou fuite de mémoire casse immédiatement le build sur GitHub Actions.

### Risques Encourus (CI/CD)

- **Faux Positifs (Flakiness) :** Risque que la CI pète à cause de fuites internes des drivers Vulkan ou du container headless.
- **Blocage de l'équipe :** Une CI trop stricte sans configuration appropriée (`.asan_ignorefile` bien rempli) empêchera les PR de passer.

### Stratégie de Validation (CI/CD)

- **Sanitizers stricts :** Remplacer `lsan_options="exitcode=0"` par `exitcode=1` dans les scripts CI.
- **Suppressions ciblées :** Remplir minutieusement `.asan_ignorefile` avec les stacktraces exactes des fuites de drivers (Mesa, llvmpipe, etc.) plutôt que d'ignorer tout en bloc.
- **Tests comparatifs :** Introduire volontairement une fuite de mémoire (comme l'oubli de delete) dans une PR de test et s'assurer que la CI passe au rouge.
