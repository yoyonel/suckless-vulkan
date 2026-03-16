# CI/CD GitHub Actions

Le projet dispose d'une chaine CI/CD simple, versionnee dans `.github/workflows/`.

La CI est executee dans une image Docker dediee (`docker/ci/Dockerfile`) basee sur `debian:bookworm-slim`, pour garantir un environnement identique entre GitHub Actions et les reproductions locales.

## Workflows

- `ci.yml` : verification continue sur `push` et `pull_request` vers `master`.
- `release.yml` : publication d'artefacts binaires sur tags `v*` (et declenchement manuel possible).
- `ci-image.yml` : build et publication de l'image CI vers GHCR.

## CI: Build et tests

Le workflow `ci.yml` execute les etapes suivantes :

1. Resolution de l'image CI (`ghcr.io/<owner>/suckless-vulkan-ci:latest`) avec fallback sur build local depuis `docker/ci/Dockerfile`.
1. Lint complet dans un conteneur CI.
1. Build+tests en matrice `Release`/`Debug` dans un conteneur CI.
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

`hadolint` analyse le `Dockerfile` a chaque execution de `just lint` via la recette
`lint-dockerfile`. La configuration se trouve dans `.hadolint.yaml` :

- `failure-threshold: error` : les warnings et infos sont affiches mais ne bloquent pas.
- Les regles DL3008/DL3013 (versionnage des paquets) sont intentionnellement ignorees
  pour cette image de developpement/CI.

### Lint des workflows GitHub Actions

`actionlint` verifie statiquement les fichiers `.github/workflows/*.yml` a chaque
execution de `just lint` via la recette `lint-actions`.

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
