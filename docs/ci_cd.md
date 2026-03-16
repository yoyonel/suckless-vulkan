# CI/CD GitHub Actions

Le projet dispose d'une chaine CI/CD simple, versionnee dans `.github/workflows/`.

## Workflows

- `ci.yml` : verification continue sur `push` et `pull_request` vers `master`.
- `release.yml` : publication d'artefacts binaires sur tags `v*` (et declenchement manuel possible).

## CI: Build et tests

Le workflow `ci.yml` execute les etapes suivantes sur `ubuntu-latest` :

1. Installation des dependances systeme (CMake, GLFW, GLM, Vulkan, headless, outils shaders).
1. Compilation des shaders GLSL en SPIR-V.
1. Configuration CMake en matrice `Release` et `Debug`.
1. Compilation des cibles.
1. Execution des tests d'integration via `ctest`.
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

## Utilisation

### Lancer la CI

- Ouvrir une pull request vers `master`, ou pousser directement sur `master`.

### Creer une release binaire

1. Creer un tag semantique, par exemple `v1.0.0`.
1. Pousser le tag: `git push origin v1.0.0`.
1. Recuperer les assets attaches a la release GitHub.

### Declenchement manuel du workflow de release

Le workflow `release.yml` expose aussi `workflow_dispatch` pour une execution manuelle depuis l'interface GitHub Actions.
