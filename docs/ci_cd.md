# CI/CD GitHub Actions

Le projet dispose d'une chaine CI/CD simple, versionnee dans `.github/workflows/`.

La CI est executee dans une image Docker dediee (`docker/ci/Dockerfile`) basee sur `debian:bookworm-slim`, pour garantir un environnement identique entre GitHub Actions et les reproductions locales.

## Workflows

- `ci.yml` : verification continue sur `push` et `pull_request` vers `master`.
- `release.yml` : publication d'artefacts binaires sur tags `v*` (et declenchement manuel possible).
- `ci-image.yml` : build et publication de l'image CI vers GHCR.

## CI: Build et tests

Le workflow `ci.yml` execute les etapes suivantes :

1. **`static-checks`** (runner hote, sans conteneur) : `hadolint` sur le Dockerfile +
   `actionlint` sur les workflows. Bloque les jobs suivants en cas d'echec.
1. **`lint`** : resolution de l'image CI puis lint complet dans le conteneur.
1. **`build-and-test`** (matrice `Release`/`Debug`) : build+tests dans le conteneur CI.
1. Upload optionnel de `test_output.png` en artefact de job.

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

### Exécution en local

Pour reproduire ces vérifications sur la machine hôte:

```bash
# ASan/UBSan pour CPU/RAM
just build-asan
just test-asan

# Validation Layers pour GPU/VRAM
just test-validation-layers
```

Voir [Tooling > Sécurité Mémoire](tooling.md#-s%C3%A9curit%C3%A9-m%C3%A9moire) pour les détails techniques.

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
