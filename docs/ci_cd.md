# CI/CD GitHub Actions

Le projet dispose d'une chaine CI/CD simple, versionnee dans `.github/workflows/`.

La CI est executee dans une image Docker dediee (`docker/ci/Dockerfile`) basee sur `debian:trixie-slim` (au lieu de `bookworm`), pour garantir un environnement identique (Vulkan 1.4) entre GitHub Actions, le poste de développement local, et les reproductions locales.

## Workflows

- `ci.yml` : verification continue sur `push` et `pull_request` vers `master`.
  Inclut un job de couverture (`coverage-report`) qui publie des artefacts de coverage.
- `release.yml` : publication d'artefacts binaires sur tags `v*` (et declenchement manuel possible).
- `ci-image.yml` : build et publication de l'image CI vers GHCR.

## CI: Build et tests

Le workflow `ci.yml` exécute les 8 jobs suivants en parallèle optimisé (~2 minutes) :

1. **`static-checks`** (runner hôte, sans conteneur) : `hadolint` sur le Dockerfile + `actionlint` sur les workflows. Bloque les jobs suivants en cas d'échec.
1. **`lint`** : vérification des formats et lints (C++, CMake, Shell, YAML, Markdown, Shaders, Python).
1. **`memory-checks-asan`** : tests de logique sous AddressSanitizer/UBSan pour détecter fuites CPU et undefined behaviors.
1. **`memory-checks-validation-layers`** : validation stricte Vulkan (`VK_LAYER_KHRONOS_validation`) sur les tests graphiques.
1. **`build-and-test`** (matrice `Release` et `Debug`) : compilation complète, exécution des tests unitaires et d'intégration avec upload systématique des captures visuelles (`test_*.png`).
1. **`coverage`** : compilation instrumentée Clang et génération du rapport de couverture HTML via `llvm-cov`.
1. **`build-tracy`** : compilation avec instrumentation Tracy (`ENABLE_TRACY=ON`), test d'intégration de trace automatisé (`just test-integration-tracy`) sous Xvfb + xdotool, et extraction/validation des captures de traces `.tracy`.

### Optimisation Image GHCR Pré-compilée

Pour éviter la recompilation locale de l'image Docker sur chaque runner GitHub Actions (gain de ~4 minutes par job), l'image de base est pré-compilée et publiée sur GitHub Container Registry (`ghcr.io/yoyonel/suckless-vulkan-ci:<tag>`). Chaque job CI résout et télécharge directement l'image en ~15 secondes via `docker pull` + `docker tag`.

Notes:

- Sous Debian Trixie, le driver Vulkan logiciel Lavapipe est nommé `lvp_icd.json`. Les scripts de CI utilisent cette configuration spécifique pour l'exécution des tests *headless*.
- Pour éviter un plantage de GLFW3 lors de l'initialisation de Wayland sans interface graphique, l'environnement CI force `XDG_SESSION_TYPE=x11`.
- Le test headless s'appuie sur `scripts/run_test_vulkan.sh`.
- La variable `VULKAN_TEST_SAVE_FRAME=1` est activee dans le job de test pour faciliter le diagnostic.
- **Transparence et Non-Interférence :** Les tests d'intégration (`EngineIntegrationTest`) ainsi que le smoke test (`smoke_test_app.sh`) utilisent des options spécifiques (`glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE)` et l'argument `--no-focus`) pour s'exécuter de façon 100% invisible. La fenêtre de test ne s'affiche pas à l'écran, ne vole pas le focus du terminal, et ne capture pas la souris. Cela permet aux développeurs (et aux pre-commit hooks) d'exécuter la suite de tests en arrière-plan (ex: `just check && just test-asan`) sans aucune perturbation du flux de travail.

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

- **Objectif:** Détecter les fuites et corruptions mémoire CPU/RAM dans la logique métier.
- **Technologie:** AddressSanitizer (ASan) et UndefinedBehaviorSanitizer (UBSan)
- **Activation:** Compilé avec `-fsanitize=address,undefined -fno-omit-frame-pointer`
- **Exécution:** Lance `scripts/ci/run_ci_asan.sh` dans le conteneur CI
- **Stratégie ISO-Local (Important) :**
  - **En CI :** Le conteneur ne disposant pas de GPU, il s'appuie sur `llvmpipe` (rasterizer logiciel CPU/LLVM) qui émet de *faux positifs* de fuites mémoire (dues aux threads JIT asynchrones). Pour garder une CI 100% stable et verte, **le job ASan en CI n'exécute QUE les tests logiques (`LogicTests`)** sans rendu.
  - **En Local (Pre-push) :** Le développeur exécute le rendu sur un vrai GPU. Un hook git `pre-push` est configuré (`.pre-commit-config.yaml`) pour forcer l'exécution locale de **tous** les tests (`just test-asan`) avant d'autoriser le push. Cela bloque la propagation d'une vraie fuite mémoire vers le dépôt.
- **Status:** Job bloquant (`continue-on-error: false`) car il ne produit plus de faux positifs.

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
