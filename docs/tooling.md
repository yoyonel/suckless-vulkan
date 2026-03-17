# Outillage et Qualité de Code

Le projet maintient des normes de qualité industrielles grâce à une suite d'outils stricts basés sur l'écosystème LLVM (`clang-tools`).

## 🏃 Task Runner : `just`

Toutes les commandes du projet sont abstraites derrière `just` (via le fichier `justfile`).

- `just help` : Affiche une aide rapide + la liste complete des recettes.
- `just build` : Compile les shaders GLSL en SPIR-V, puis compile le code C++.
- `just run` : Compile et lance l'application.
- `just test` : Compile puis exécute les tests d'intégration via `ctest`.
- `just test-all` : Flow recommandé (`EngineIntegrationTest` puis `LogicTests`).
- `just test-integration` : Exécute uniquement `EngineIntegrationTest`.
- `just test-logic` : Exécute uniquement `LogicTests`.
- `just coverage` : Exécute `LogicTests` avec instrumentation coverage et génère les rapports.
- `just format` : Formate tous les fichiers owner du repo.
- `just lint` : Lance le lint complet sur tous les fichiers owner du repo.
- `just check` : Exécute Format -> Lint -> Tests.
- `just rebuild` : Nettoie le cache CMake et recompile de zéro.

### Couverture "owner files"

Le pipeline `format`/`lint` couvre explicitement :

- C/C++ et GLSL (`clang-format`, `clang-tidy`, `glslangValidator`)
- CMake (`cmake-format`, `cmake-lint`)
- Scripts shell (`shfmt`, `shellcheck`)
- YAML (`yamlfmt`, `yamllint`)
- Markdown (`mdformat`, `pymarkdown`)
- `justfile` (`just --fmt`, `just --fmt --check`)
- Dockerfile (`hadolint`, config `.hadolint.yaml`)
- GitHub Actions workflows (`actionlint`)

## 🔒 Garde-fous locaux : `pre-commit`

Le depot utilise aussi `pre-commit` pour imposer automatiquement format/lint avant commit/push.

### Installation

- `just pre-commit-install`

Cette commande installe deux hooks Git locaux:

- `pre-commit` : lance `just format`, puis `just lint-fast`.
- `pre-push` : lance `just test`.

Pour executer la gate complete locale (comme une verification CI stricte):

- `just lint && just test`

### Execution manuelle

- `just pre-commit-run` : execute tous les hooks `pre-commit` sur tous les fichiers.
- `just pre-push-run` : execute le hook `pre-push` sur tous les fichiers.

## 🧹 Formatage : `clang-format`

Géré par `.clang-format`. Il impose :

- Une indentation de 4 espaces.
- Une limite de 160 caractères par ligne.
- Le tri automatique des `#include`.
- *Note :* `clang-format` est configuré pour formater également le code GLSL des shaders (`.vert`, `.frag`) grâce à sa proximité syntaxique avec le C++.

## 🔬 Analyse Statique : `clang-tidy`

L'analyse statique s'appuie sur le fichier `compile_commands.json` généré par CMake.
Les dépendances tierces rangées sous `ext/` sont déclarées en includes système et référencées explicitement depuis `ext/` afin d'éviter toute confusion avec le code métier et de mieux se comporter dans l'analyse statique hors contexte CMake complet.
La recette `just lint` force aussi l'exclusion de `ext/` dans les diagnostics remontés par `clang-tidy`, afin de ne conserver que les avertissements relevant du projet.
La sortie utilise aussi le mode silencieux de `clang-tidy` pour éviter les compteurs de warnings issus des bibliothèques tierces parcourues transitivement.
Plusieurs règles ont été consciemment désactivées dans `.clang-tidy` pour s'adapter aux spécificités de l'API Vulkan en C++ :

| Règle désactivée | Raison de la suppression |
| :--- | :--- |
| `readability-implicit-bool-conversion` | Permet d'écrire `if (!file)` au lieu de l'encombrant `if (file == nullptr)`. |
| `bugprone-invalid-enum-default-initialization` | Désactivée car l'initialisation globale `{}` des structures Vulkan initialise les énumérations à `0`, ce qui provoque de faux positifs pour les flags qui n'ont pas de valeur `0` explicite (comme `VkSampleCountFlagBits`). |

## 🧪 Strategie de Tests

Le projet contient maintenant deux types de tests CTest complementaires :

- `EngineIntegrationTest` (binaire `unit_tests`) : test d'intégration Vulkan bout-en-bout.
- `LogicTests` (binaire `logic_tests`) : tests logiques rapides sans rendu GPU.

### EngineIntegrationTest

Le binaire `unit_tests` est un test d'intégration qui :

- Initialise complètement le moteur Vulkan.
- Rend une frame.
- Relit l'image de swapchain dans un buffer CPU.
- Vérifie que le contenu produit n'est pas vide.

Par defaut, ce test ne sauvegarde pas d'image PNG pour eviter de polluer le depot avec des artefacts de run.
La capture disque est activee uniquement si la variable d'environnement `VULKAN_TEST_SAVE_FRAME` est definie :

- Exemple : `VULKAN_TEST_SAVE_FRAME=1 just test`
- Fichier genere : `test_output.png` a la racine du projet (working directory de CTest configuree sur `${CMAKE_SOURCE_DIR}`).

`ctest` passe par le script `scripts/run_test_vulkan.sh`, qui force un mode headless avec `xvfb-run` et Lavapipe quand l'environnement ne fournit pas d'affichage.

### LogicTests

Le binaire `logic_tests` couvre les comportements applicatifs ajoutés recemment :

- Gestion clavier runtime (`Space`, `Up`, `Down`, `R`, `F11`, `Esc`).
- Bascule fullscreen/fenetre et restauration de geometrie.
- Logging structure (callback, filtrage par niveau, formatage message).

Le flow cible pour les devs est :

- `just test-all`

## 📊 Couverture de Code (Coverage)

Le projet supporte deux outils pour mesurer la couverture de code sur les tests :

### Option 1: GCovr (compatible GCC et Clang)

Génère des rapports texte, XML (Cobertura) et HTML :

```bash
just coverage
```

Rapports générés dans `build/coverage/reports/` :

- `coverage.txt` : résumé texte simplifié
- `coverage.xml` : Cobertura XML pour imports CI
- `coverage.html` : rapport HTML avec annotations source
- `coverage.json` : données JSON brutes

**Limitations:** Couverture seulement des tests logiques (LogicTests), vk_engine.cpp non instrumenté.

### Option 2: LLVM-Cov (recommandée, clang requis)

Rapport détaillé avec console output formaté + HTML :

```bash
just coverage-llvm
```

Rapports générés dans `build/coverage-llvm/`:

- `coverage_report/index.html` : HTML interactif avec couverture source
- Console output : tableau résumé avec Regions/Functions/Lines/Branches

**Avantages:**

- Couverture **complète** : LogicTests + EngineIntegrationTest
- Inclut tous les fichiers métier := app_log.cpp, runtime_controls.cpp, vk_engine.cpp, icosphere.h
- Meilleur détail : Regions + Functions + Lines + Branches
- Formatage console lisible

**Exemple de sortie:**

```text
📊 LLVM CODE COVERAGE SUMMARY REPORT (All Tests)
══════════════════════════════════════════════════════════════════════════════

Filename                 Regions  Missed Regions  Cover   Functions  Executed  Lines   Cover
─────────────────────────────────────────────────────────────────────────────────────────────
app_log.cpp                  67           21    68.66%        11   100.00%      121   79.34%
runtime_controls.cpp         56           12    78.57%        11    27.27%       79   64.56%
vk_engine.cpp               496          118    76.21%        39    97.44%     1010   78.71%
icosphere.h                  12            0   100.00%         2   100.00%       43  100.00%
─────────────────────────────────────────────────────────────────────────────────────────────
TOTAL                       631          151    76.07%        63    85.71%     1253   78.61%
```

**KPI:** 78.61% du code métier est couvert par les tests.

## �️ Sécurité Mémoire : Sanitizers et Validation Layers

### AddressSanitizer + UndefinedBehaviorSanitizer (RAM/CPU)

Le projet supporte la détection des fuites mémoire et des comportements non-définis en C++ via les sanitizers GCC/Clang.

Compilation + exécution avec ASan/UBSan :

```bash
just build-asan
just run-asan
just test-asan
```

Ces recettes configurent CMake avec `-DENABLE_SANITIZERS=ON`, qui applique les flags :

- `-fsanitize=address,undefined`
- `-fno-omit-frame-pointer` (pour les stack traces lisibles)

À l'exécution, tout comportement mémoire ou arithmétique invalide est signalé avec un stack trace
détaillé. Les binaires ASan restent débugables avec `lldb` ou `gdb`.

**Suppression des false-positives du Vulkan SDK:** Le fichier `.asan_ignorefile` supprime les
allocations triviales du Vulkan driver sur shutdown (non-responsabilité de l'application). Cette
suppression est automatiquement appliquée par `just test-asan` via `LSAN_OPTIONS`.

### Vulkan Validation Layers (VRAM/GPU)

Les Vulkan Validation Layers (fournis par le Khronos) détectent statiquement les erreurs d'utilisation
de l'API Vulkan (commande buffers mal formés, synchronisation erronée, etc.) et peuvent tracker
la mémoire GPU mais surtout les erreurs de protocole.

Tester avec les Validation Layers activés :

```bash
just test-validation-layers
```

Ou manuellement, depuis n'importe quel binaire :

```bash
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation ./build/debug/vulkan_app
```

Les Validation Layers produisent beaucoup de sortie diagnostique; si elle gène, réduire le verbosité :

```bash
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation \
  VK_LAYER_KHRONOS_VALIDATION_DEBUG_ACTION=VK_DBG_LAYER_ACTION_LOG_MSG \
  ./build/debug/vulkan_app
```

## �🚦 CI/CD GitHub Actions

Le depot inclut trois workflows sous `.github/workflows/` :

- `ci.yml` : verification continue sur `push`/`pull_request` vers `master`. Tourne a
  l'interieur d'une image Docker Debian (`docker/ci/Dockerfile`) pour garantir la
  reproducibilite locale/remote. Etapes : lint, build+test en matrice `Release`/`Debug`.
- `ci-image.yml` : build et publication de l'image CI vers GHCR (`ghcr.io`).
- `release.yml` : CD binaires sur tags `v*` : compile Release+Debug, archive et attache
  les executables a une GitHub Release.

La meme chaine peut etre reproduite en local sans aucune dependance vers GitHub :

```bash
just ci-docker-all
```

See `docs/ci_cd.md` for full operational details.

## 🎨 Validation des Shaders : `glslangValidator`

Les shaders ne sont pas uniquement compilés, ils sont "lintés" via l'outil officiel du Khronos Group.
L'option `-V` est systématiquement passée à `glslangValidator` pour forcer la sémantique Vulkan (au lieu de la sémantique OpenGL par défaut), permettant notamment la bonne reconnaissance de variables comme `gl_VertexIndex`.
Les recettes de lint envoient explicitement la sortie SPIR-V vers `/dev/null` pour eviter la creation de doublons `./vert.spv` et `./frag.spv` a la racine.
