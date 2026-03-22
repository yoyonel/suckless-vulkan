# CI/CD GitHub Actions

Le projet dispose d'une chaine CI/CD simple, versionnee dans `.github/workflows/`.

La CI est executee dans une image Docker dediee (`docker/ci/Dockerfile`) basee sur `debian:bookworm-slim`, pour garantir un environnement identique entre GitHub Actions et les reproductions locales.

## Workflows

- `ci.yml` : verification continue sur `push` et `pull_request` vers `master`.
  Inclut un job de couverture (`coverage-report`) qui publie des artefacts de coverage.
- `release.yml` : publication d'artefacts binaires sur tags `v*` (et declenchement manuel possible).
- `ci-image.yml` : build et publication de l'image CI vers GHCR.

## CI: Build et tests

Le workflow `ci.yml` execute les etapes suivantes :

1. **`static-checks`** (runner hote, sans conteneur) : `hadolint` sur le Dockerfile +
   `actionlint` sur les workflows. Bloque les jobs suivants en cas d'echec.
1. **`lint`** : resolution de l'image CI puis lint complet dans le conteneur.
1. **`build-and-test`** (matrice `Release`/`Debug`) : build+tests dans le conteneur CI.
1. **Upload systématique** des captures de tests (`test_*.png`) en artefact de job en cas d'échec ou de succès.

Notes:

- Le test headless s'appuie sur `scripts/run_test_vulkan.sh`.
- La variable `VULKAN_TEST_SAVE_FRAME=1` est activee dans le job de test pour faciliter le diagnostic.

## CD: Release d'artefacts

Le workflow `release.yml` construit et publie deux archives Linux x86_64 :

- `vulkan_base-linux-x86_64-release.tar.gz`
- `vulkan_base-linux-x86_64-debug.tar.gz`

Chaque archive contient:

- `vulkan_app`
- `unit_tests`
- `vert.spv`
- `frag.spv`

Sur un tag `v*`, les archives sont attachees automatiquement a la GitHub Release correspondante.

## Securite des conteneurs

Les conteneurs CI sont toujours executes en **non-root** :

- Le `Dockerfile` declare `USER ci` (uid 1001) comme utilisateur par defaut.
- Les `docker run` passent `--user "$(id -u):$(id -g)" -e HOME=/tmp` pour que les
  fichiers crees dans le volume monte appartiennent a l'utilisateur hote, jamais a root.

### Lint et securite Dockerfile

`hadolint` analyse le `Dockerfile` a **deux niveaux** :

- En **CI GitHub** (`ci.yml` job `static-checks` et `ci-image.yml` job `lint-dockerfile`)
  directement sur le runner hote, **avant** tout build ou pull de l'image. Cela garantit
  que le Dockerfile est valide avant d'etre utilise ou publie sur GHCR.
- En **local** via `just lint-dockerfile` (utilise `hadolint` embarque dans l'image CI).

La configuration se trouve dans `.hadolint.yaml` :

- `failure-threshold: error` : les warnings et infos sont affiches mais ne bloquent pas.
- Les regles DL3008/DL3013 (versionnage des paquets) sont intentionnellement ignorees
  pour cette image de developpement/CI.

### Lint des workflows GitHub Actions

`actionlint` verifie statiquement les fichiers `.github/workflows/*.yml` :

- En **CI GitHub** (`ci.yml` job `static-checks`) sur le runner hote, avant tout job
  dependant du conteneur.
- En **local** via `just lint-actions`.

## Vérifications de mémoire (ASan/UBSan & Validation Layers)

### Jobs optionnels de détection des fuites mémoires

Après le job `static-checks`, deux jobs optionnels (non-bloquants) diagnostiquent les problèmes de mémoire :

#### `memory-checks-asan`

- **Objectif:** Détecter les fuites et corruptions mémoire CPU/RAM
- **Technologie:** AddressSanitizer (ASan) et UndefinedBehaviorSanitizer (UBSan)
- **Activation:** Compilé avec `-fsanitize=address,undefined -fno-omit-frame-pointer`
- **Exécution:** Lance `just test-asan` dans le conteneur CI
- **Sortie:**
  - ASan/UBSan imprime les erreurs détectées pendant l'exécution des tests
  - Les rapports incluent des stack traces annotées pour localiser les fuites
- **Status:** `continue-on-error: true` — les échecs ne bloquent pas la CI

#### `memory-checks-validation-layers`

- **Objectif:** Détecter les erreurs GPU et fuites VRAM via Vulkan Validation Layers
- **Technologie:** Vulkan Validation Layer (KHRONOS)
- **Activation:** Variable d'environnement `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`
- **Exécution:** Lance `just test-validation-layers` dans le conteneur CI
- **Sortie:**
  - Les Validation Layers rapportent les violations des règles Vulkan
  - Les messages incluent des avertissements et erreurs GPU/VRAM
- **Status:** `continue-on-error: true` — les échecs ne bloquent pas la CI

### Job: Rapport de couverture avec LLVM-Cov

#### `coverage-report-llvm`

- **Objectif:** Générer un rapport détaillé de couverture de code pour tous les tests
- **Technologie:** LLVM-Cov avec profiling par instrumentation (`fprofile-instr-generate`)
- **Activation:** Compilé avec `-fprofile-instr-generate -fcoverage-mapping`
- **Exécution:** Lance `scripts/ci/run_ci_coverage_llvm.sh` dans le conteneur CI
- **Tests inclus:** LogicTests + EngineIntegrationTest (couverture complète du code métier)
- **Sortie principale:**
  - Rapports en HTML dans `build/coverage-llvm/coverage_report/` (source annotations)
  - Résumé console montrant Regions/Functions/Lines/Branches par fichier

**Exemple résumé CLI:**

```text
Filename                 Regions  Missed Regions  Cover   Functions  Executed  Lines   Cover
─────────────────────────────────────────────────────────────────────────────────────────────
app_log.cpp                  67           21    68.66%        11   100.00%      121   79.34%
runtime_controls.cpp         56           12    78.57%        11    27.27%       79   64.56%
vk_engine_init.cpp         ...          ...      ...         ...      ...      ...      ...
vk_engine_frame.cpp        ...          ...      ...         ...      ...      ...      ...
vk_engine_envmap.cpp       ...          ...      ...         ...      ...      ...      ...
─────────────────────────────────────────────────────────────────────────────────────────────
TOTAL                       631          151    76.07%        63    85.71%     1253   78.61%
```

**Artefacts publiés:** Dossier HTML complet `coverage-report-llvm-html` pour historique de couverture.

**Status:** Job bloquant si compilation/tests échouent; obligatoire pour PR/push vers `master`.

### Job: Rapport de couverture avec GCovr

#### `coverage-report`

- **Objectif:** Couverture alternative avec GCovr (compatible GCC)

- **Technologie:** GCovr avec flags coverage GCC/Clang (`--coverage`)

- **Sortie:** Rapports texte/XML/HTML dans `build/coverage/reports/`

- **Artefacts publiés:** coverage.txt, coverage.xml, coverage.html

- **Note:** Tests LogicTests uniquement (couverture partielle)

- `id: lint-owner-files-fast` : `just lint-fast` (format + lint rapide) sur `pre-commit`.

- `id: test-before-push` : `just test` (tests de non-régression logique et visuelle) sur `pre-push`.

### Intégration Visuelle (Regression Testing)

Le projet utilise un système de **Visual Regression Testing** intégré à la CI :

- **Échec bloquant** : Si un changement de code modifie le rendu au-delà de la tolérance (driver parity), le job `build-and-test` échoue.
- **Artifacts de diagnostic** : En cas d'échec, les images `test_*.png` sont disponibles dans les artefacts GitHub Actions pour comparer visuellement l'erreur.
- **Mise à jour des références** : Si le changement est légitime, mettre à jour les références locales avec `SVK_UPDATE_REFERENCES=1 just test-integration` et committer les nouveaux fichiers dans `tests/references/`.

### Synchronisation automatique des références avec Docker CI

Pour éviter les écarts entre rendu local et rendu CI (lavapipe/xvfb), le projet fournit une synchronisation dédiée en conteneur:

- `just sync-test-references` :
  1. exécute les tests dans Docker avec `SVK_UPDATE_REFERENCES=1` pour régénérer les PNG de `tests/references/`,
  1. relance immédiatement les tests en mode strict (sans update) pour valider que la nouvelle baseline passe,
  1. met à jour `tests/references/manifest.sha256` avec les checksums des références.
- `just sync-test-references-all` : exécute la synchronisation puis `just ci-docker-all` (lint + Release + Debug).

Important:

- Les références ne sont pas stockées "dans l'image Docker"; elles restent dans le repository et sont partagées au conteneur via le volume `-v $PWD:/work`.
- Toute mise à jour des PNG et du manifest doit être commitée pour garder la CI GitHub cohérente.

### Exécution en local

Pour reproduire la couverture de code sur la machine hôte:

```bash
# Generer rapport LLVM-Cov (recommandé, clang requis)
just coverage-llvm

# Generer rapport GCovr (compatible GCC/Clang)
just coverage
```

Voir [Outillage & Qualite — Couverture](tooling.md#%F0%9F%93%8A-couverture-de-code-coverage) pour les details techniques.

### Exécution en local (mémoire)

Pour reproduire les vérifications de sécurité mémoire sur la machine hôte:

```bash
# ASan/UBSan pour CPU/RAM
just build-asan
just test-asan

# Validation Layers pour GPU/VRAM
just test-validation-layers
```

Voir [Outillage & Qualite](tooling.md) pour les details techniques.

## Stratégie de release (Trunk-Based Development)

See `.github/DEVELOPMENT.md` for the full TBD policy including:

- **Release process:** Tags from `master` only; GitHub Actions auto-publishes binaries
- **Hotfix workflow:** Fixes applied to `master` → tagged → released (no long-lived release branches)
- **Commit discipline:** Pre-commit checks ensure `master` never breaks
- **Short-lived branches:** Feature branches exist only for code review (\<3 days)

Key principle: **Releases trace directly back to `master` lineage**.

## Utilisation

### Reproduire la CI en local

- `just ci-docker-lint`
- `just ci-docker Release`
- `just ci-docker Debug`
- `just ci-docker-all`

### Lancer la CI GitHub

- Ouvrir une pull request vers `master`, ou pousser directement sur `master`.

### Creer une release binaire

1. Creer un tag semantique, par exemple `v1.0.0`.
1. Pousser le tag: `git push origin v1.0.0`.
1. Recuperer les assets attaches a la release GitHub.

### Declenchement manuel du workflow de release

Le workflow `release.yml` expose aussi `workflow_dispatch` pour une execution manuelle depuis l'interface GitHub Actions.
