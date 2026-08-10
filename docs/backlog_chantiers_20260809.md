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

## 3. ~~Refonte de la CI/CD (ISO Local/CI)~~ [FAIT]

### Objectifs & Avantages (CI/CD)

- **Fiabilité Absolue :** Avoir une CI 100% fiable sans ignorer les faux positifs d'ASan sur llvmpipe, tout en garantissant l'absence de fuites en local.
- **Fail-Fast :** S'assurer que toute régression ou fuite de mémoire casse immédiatement le build sur GitHub Actions (pour la logique pure) ou en local (pour le rendu).

### Risques Encourus (CI/CD)

- **Deadlocks llvmpipe :** Tenter de supprimer les fuites llvmpipe via variables d'environnement (`DISABLE_LSAN_SCOPE`) ou configurations a causé des deadlocks du JIT shader compiler.
- **Faux Positifs (Flakiness) :** Risque que la CI pète à cause de fuites internes des drivers Vulkan ou du container headless.

### Stratégie de Validation Finale (ISO Local)

*Stratégie implémentée suite à l'abandon de `.asan_ignorefile` (cause deadlocks).*

- **Ségrégation en CI :** Le job `memory-checks-asan` est devenu strict (`continue-on-error: false`) mais **restreint** aux tests unitaires de logique (`LogicTests`), évitant ainsi le lancement de `llvmpipe` en CI et garantissant un résultat 100% vert.
- **Vérification Locale (Pre-push) :** Configuration d'un hook git `pre-push` via `.pre-commit-config.yaml` qui oblige le développeur à faire passer TOUS les tests (`just test-asan`, incluant l'intégration et le rendu sur vrai GPU) localement avant d'autoriser un push vers le dépôt distant.
- **Tests comparatifs :** Injection volontaire d'une vraie fuite de mémoire (`new int(42);`) prouvant que le hook `pre-push` bloque instantanément le push.
