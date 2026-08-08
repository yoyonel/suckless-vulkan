set shell := ["bash", "-c"]

renderdoc_bin := "qrenderdoc"

# Affiche une aide rapide + la liste complète des recettes.
help:
    @echo ""
    @echo "suckless-vulkan - aide just"
    @echo ""
    @echo "Recettes les plus utiles:"
    @echo "  just build                # build release"
    @echo "  just run                  # build + execution"
    @echo "  just build-tracy          # build RelWithDebInfo + Tracy client"
    @echo "  just build-tracy-capture  # build CLI tracy-capture (sans GUI)"
    @echo "  just build-tracy-profiler # build Tracy Profiler v0.13.1 (X11 legacy)"
    @echo "  just test-integration-tracy # scenario auto Xvfb+xdotool + trace .tracy"
    @echo "  just renderdoc            # launch qrenderdoc with debug build"
    @echo "  just renderdoc-debug-shaders # launch qrenderdoc with shader debug info (-g -O0/-Od)"
    @echo "  just test                 # tous les tests CTest (release)"
    @echo "  just shaders-ibl          # compile the 5 IBL compute shaders to SPIR-V"
    @echo "  just test-all             # flow explicite: integration + logic"
    @echo "  just test-integration     # EngineIntegrationTest uniquement"
    @echo "  just sync-test-references # regenere references via Docker CI + verifie"
    @echo "  just test-logic           # LogicTests uniquement"
    @echo "  just coverage             # rapport de couverture tests logiques"
    @echo "  just lint                 # lint complet"
    @echo "  just check                # format + lint + test"
    @echo "  just verify-ibl           # compare OGL vs Vulkan IBL maps"
    @echo ""
    @echo "Toutes les recettes disponibles:"
    @just --list

# --- CONFIGURATION ---

# Configure CMake (nécessaire pour générer compile_commands.json pour clang-tidy)
configure:
    @if command -v ccache >/dev/null 2>&1; then \
        cmake -B build/release -S . -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER_LAUNCHER=ccache; \
    else \
        cmake -B build/release -S . -DCMAKE_BUILD_TYPE=Release; \
    fi

# Configure un build Debug.
configure-debug:
    @if command -v ccache >/dev/null 2>&1; then \
        cmake -B build/debug -S . -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER_LAUNCHER=ccache; \
    else \
        cmake -B build/debug -S . -DCMAKE_BUILD_TYPE=Debug; \
    fi

# Configure un build Debug avec sanitizers (ASan/UBSan).
configure-asan:
    @if command -v ccache >/dev/null 2>&1; then \
        cmake -B build/asan -S . -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON -DCMAKE_CXX_COMPILER_LAUNCHER=ccache; \
    else \
        cmake -B build/asan -S . -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON; \
    fi

# Configure un build instrumente pour llvm-cov (clang obligatoire).
configure-coverage:
    @if command -v ccache >/dev/null 2>&1; then \
        cmake -B build/coverage -S . -DCMAKE_BUILD_TYPE=Debug -DENABLE_LLVM_COV=ON -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_COMPILER_LAUNCHER=ccache; \
    else \
        cmake -B build/coverage -S . -DCMAKE_BUILD_TYPE=Debug -DENABLE_LLVM_COV=ON -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++; \
    fi

# Configure un build RelWithDebInfo avec Tracy active.
configure-tracy:
    @if command -v ccache >/dev/null 2>&1; then \
        cmake -B build/tracy -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_TRACY=ON -DCMAKE_CXX_COMPILER_LAUNCHER=ccache; \
    else \
        cmake -B build/tracy -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_TRACY=ON; \
    fi

# Configure le profiler Tracy upstream en backend X11 legacy.
configure-tracy-profiler: configure-tracy
    @cmake -B build/tracy-profiler -S build/tracy/_deps/tracy-src/profiler -DCMAKE_BUILD_TYPE=Release -DLEGACY=ON

# Configure l'outil CLI tracy-capture upstream.
configure-tracy-capture: configure-tracy
    @cmake -B build/tracy-capture -S build/tracy/_deps/tracy-src/capture -DCMAKE_BUILD_TYPE=Release

# --- COMPILATION ---

# Compile les shaders GLSL en SPIR-V (+ asm si glslc disponible).
shaders:
    @echo "Compilation des shaders..."
    @scripts/compile_shaders.sh raster
    @scripts/compile_shaders.sh ibl

# Compile les shaders GLSL en SPIR-V orienté debug RenderDoc (source-level):
# - glslc: -g -O0
# - glslangValidator: -g -Od

# Compile les 5 shaders compute IBL (luminance x2, BRDF LUT, irradiance, specular).
shaders-ibl:
    @echo "Compilation des shaders compute IBL..."
    @scripts/compile_shaders.sh ibl

# Compile les 5 shaders compute IBL en mode debug RenderDoc (-g -O0 / -g -Od).
shaders-debug-ibl:
    @echo "Compilation des shaders compute IBL en mode debug RenderDoc..."
    @scripts/compile_shaders.sh ibl-debug

shaders-debug:
    @echo "Compilation des shaders en mode debug RenderDoc (-g, sans optimisations)..."
    @scripts/compile_shaders.sh raster-debug

# Compile l'application en Release.
build: configure shaders
    @echo "Compilation Release..."
    @cmake --build build/release -j$(nproc)

# Compile l'application en Debug.
build-debug: configure-debug shaders
    @echo "Compilation Debug..."
    @cmake --build build/debug -j$(nproc)

# Compile l'application en Debug avec shaders compilés pour un debug pixel lisible dans RenderDoc.
build-debug-renderdoc: configure-debug shaders-debug
    @echo "Compilation Debug (profil RenderDoc shader debug)..."
    @cmake --build build/debug -j$(nproc)

# Compile l'application en Debug avec ASan/UBSan.
build-asan: configure-asan shaders
    @echo "Compilation Debug avec ASan + UBSan..."
    @cmake --build build/asan -j$(nproc)

# Compile les cibles avec instrumentation de couverture (llvm-cov).
build-coverage: configure-coverage shaders
    @echo "Compilation Debug avec couverture LLVM..."
    @cmake --build build/coverage --parallel

# Compile l'application avec Tracy active.
build-tracy: configure-tracy shaders
    @echo "Compilation RelWithDebInfo avec Tracy..."
    @cmake --build build/tracy -j$(nproc)

# Compile le binaire tracy-profiler upstream en mode X11 legacy.
build-tracy-profiler: configure-tracy-profiler
    @echo "Compilation du Tracy Profiler (v0.13.1, X11 legacy)..."
    @cmake --build build/tracy-profiler -j$(nproc)

# Compile l'outil CLI tracy-capture upstream.
build-tracy-capture: configure-tracy-capture
    @echo "Compilation de tracy-capture (CLI)..."
    @cmake --build build/tracy-capture -j$(nproc)

# --- EXECUTION & DEBUG ---

# Exécute l'application release.
run args="": build
    @./build/release/vulkan_app {{ args }}

# Exécute l'application compilée avec ASan/UBSan.
run-asan: build-asan
    @echo "Exécution avec AddressSanitizer + UndefinedBehaviorSanitizer..."
    @./build/asan/vulkan_app

# Exécute l'application instrumentée Tracy.
run-tracy args="": build-tracy
    @./build/tracy/vulkan_app {{ args }}

# Lance le profiler Tracy compilé localement.
tracy-profiler: build-tracy-profiler
    @./build/tracy-profiler/tracy-profiler

# Lance un scenario d'integration automatise Tracy (Xvfb + xdotool + capture CLI).
test-integration-tracy capture_seconds="45" trace_file="build/tracy/integration.tracy": build-tracy build-tracy-capture
    @scripts/test_integration_tracy.sh "{{ capture_seconds }}" "{{ trace_file }}"

# Utilisation : just renderdoc_bin=/chemin/vers/qrenderdoc renderdoc
renderdoc: build-debug-renderdoc
    @{{ renderdoc_bin }} --working-dir . ./build/debug/vulkan_app

# Utilisation : just renderdoc_bin=/chemin/vers/qrenderdoc renderdoc-debug-shaders

# Lance qrenderdoc avec shaders compilés en -g sans optimisation (plus lisible en Pixel Debugger).
renderdoc-debug-shaders: build-debug-renderdoc
    @{{ renderdoc_bin }} --working-dir . ./build/debug/vulkan_app

# Exécute tous les tests CTest du build release.
test-tracy: build-tracy
    @scripts/smoke_test_app.sh ./build/tracy/vulkan_app

test: build
    @ctest --test-dir build/release --output-on-failure

# Exécute uniquement le test d'intégration de rendu.
test-integration: build
    @ctest --test-dir build/release --output-on-failure -R EngineIntegrationTest

# Régénère les références visuelles en environnement Docker CI puis valide en mode strict.
sync-test-references build_type="Release": ci-image-build
    @echo "Synchronisation des references de rendu dans Docker ({{ build_type }})..."
    @docker run --rm \
        --user "$(id -u):$(id -g)" \
        -e CI=true \
        -e HOME=/tmp \
        -v "$PWD:/work" \
        -w /work \
        local/suckless-vulkan-ci:latest \
        bash -lc "chmod +x scripts/ci/sync_test_references.sh scripts/ci/run_ci_build_and_test.sh && scripts/ci/sync_test_references.sh {{ build_type }}"

# Régénère les références en Release, puis valide la CI Docker complète (Release+Debug).
sync-test-references-all: sync-test-references
    @just ci-docker-all

# Exécute uniquement les tests logiques/unitaires purs.
test-logic: build
    @ctest --test-dir build/release --output-on-failure -R LogicTests

# Exécute le flow de tests recommandé: intégration puis logique.
test-all: build
    @echo "Execution du flow complet: EngineIntegrationTest + LogicTests"
    @ctest --test-dir build/release --output-on-failure -R EngineIntegrationTest
    @ctest --test-dir build/release --output-on-failure -R LogicTests

# Tests iteration rapides: LogicTests toujours, IntegrationTest seulement si rendu/Vulkan touche.
test-iter: build
    @echo "Execution rapide: LogicTests + IntegrationTest conditionnel"
    @ctest --test-dir build/release --output-on-failure -R LogicTests
    @changed_render=$(git diff --name-only --diff-filter=ACMR HEAD -- 'src/vk_engine.cpp' 'src/vk_engine.h' 'src/main.cpp' 'shaders/*.vert' 'shaders/*.frag'); \
    if [ -n "${changed_render}" ]; then \
        echo "Fichiers rendu detectes: execution EngineIntegrationTest."; \
        ctest --test-dir build/release --output-on-failure -R EngineIntegrationTest; \
    else \
        echo "Aucun fichier rendu modifie: EngineIntegrationTest saute en iteration."; \
    fi

# Exécute les tests sous ASan/UBSan.
test-asan: build-asan
    @echo "Tests avec AddressSanitizer + UndefinedBehaviorSanitizer..."
    @LSAN_OPTIONS=suppressions=./.asan_ignorefile:report_objects=1 \
        ctest --test-dir build/asan --output-on-failure

# Exécute les tests avec Vulkan Validation Layers activées.
test-validation-layers: build-debug
    @echo "Tests avec Vulkan Validation Layers..."
    @VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation \
        ctest --test-dir build/debug --output-on-failure

# Exécute TOUS les tests sur le build llvm-cov instrumenté (LogicTests + EngineIntegrationTest).
test-coverage: build-coverage
    @mkdir -p build/coverage
    @echo "Running tests with LLVM profiling..."
    @LLVM_PROFILE_FILE='{{ justfile_directory() }}/build/coverage/test_%p.profraw' ctest --test-dir build/coverage --output-on-failure
    @echo "Merging profile data..."
    @llvm-profdata merge -sparse build/coverage/*.profraw -o build/coverage/coverage.profdata

# Génère des rapports LLVM-cov (HTML + résumé console formaté)
coverage-report: test-coverage
    @echo "Generating HTML report..."
    @mkdir -p build/coverage/coverage_report
    @llvm-cov show -format=html \
        -instr-profile=build/coverage/coverage.profdata \
        build/coverage/unit_tests \
        -output-dir=build/coverage/coverage_report \
        -ignore-filename-regex='(tests/|ext/)' > /dev/null 2>&1
    @echo ""
    @echo "════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════"
    @echo "📊 LLVM CODE COVERAGE SUMMARY REPORT (All Tests: LogicTests + EngineIntegrationTest)"
    @echo "════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════"
    @echo ""
    @llvm-cov report \
        -instr-profile=build/coverage/coverage.profdata \
        build/coverage/unit_tests \
        build/coverage/logic_tests \
        -ignore-filename-regex='(tests/|ext/)' || true
    @echo ""
    @echo "════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════"
    @echo "🔗 HTML Report: build/coverage/coverage_report/index.html"
    @echo "════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════"
    @echo ""

# Flow complet de couverture avec llvm-cov.
coverage: coverage-report

# --- NOUVELLES RECETTES DE QUALITÉ DE CODE ---

# Formate le code C/C++ et les shaders.
format-code:
    @echo "Formatage du code C et des Shaders..."
    @clang-format -i src/*.cpp src/*.h tests/*.cpp shaders/*.vert shaders/*.frag
    @echo "Formatage terminé."

# Formate les fichiers CMake.
format-cmake:
    @echo "Formatage des fichiers CMake..."
    @uvx --from cmakelang cmake-format -i CMakeLists.txt

# Formate les scripts shell.
format-shell:
    @echo "Formatage des scripts shell..."
    @uvx --from shfmt-py shfmt -w scripts/*.sh

# Formate les fichiers YAML.
format-yaml:
    @echo "Formatage des fichiers YAML..."
    @npx --yes prettier --write mkdocs.yml .github/workflows/*.yml .pre-commit-config.yaml

# Formate le justfile.
format-just:
    @echo "Formatage du justfile..."
    @JUST_UNSTABLE=1 just --fmt

# Formate le code Python.
format-python:
    @echo "Formatage du code Python..."
    @uvx ruff format scripts/

# Formate la documentation Markdown.
format-docs:
    @echo "Formatage du Markdown avec mdformat..."
    @uvx mdformat docs/

# Lance tous les formateurs.
format: format-code format-cmake format-shell format-yaml format-docs format-just format-python

# Lance clang-tidy en parallèle sur tous les fichiers C/C++ du projet (hors ext). En CI (CI=true), cmake est regenere dans un dossier temporaire (chemins natifs au conteneur). En local, build/release est reutilise pour la vitesse. Parallelise avec xargs -P $(nproc) pour utiliser tous les cores.
lint-c:
    @if [ "${CI:-}" = "true" ]; then \
        build_dir=$(mktemp -d -t clang-tidy-XXXXXX); \
        cmake -S . -B "${build_dir}" -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON >/dev/null; \
    elif [ ! -f build/release/compile_commands.json ]; then \
        echo "compile_commands.json manquant dans build/release: configuration automatique..."; \
        just configure >/dev/null; \
        build_dir=build/release; \
    else \
        build_dir=build/release; \
    fi; \
    exclude_header_filter=""; \
    if clang-tidy --help 2>&1 | grep -q -- '--exclude-header-filter'; then \
        exclude_header_filter="--exclude-header-filter=(.*/)?ext/.*"; \
    fi; \
    find src tests -name '*.cpp' -type f | sort | xargs -P `nproc` -I {} clang-tidy -quiet -p "${build_dir}" {} --header-filter='(src/.*|tests/.*)' --warnings-as-errors='*' ${exclude_header_filter}

# Lance clang-tidy uniquement sur les fichiers C/C++ modifies (rapide pour iteration). Parallelise avec xargs -P $(nproc). Meme logique CI/local que lint-c pour les chemins compile_commands.
lint-c-changed:
    @if [ "${CI:-}" = "true" ]; then \
        build_dir=$(mktemp -d -t clang-tidy-XXXXXX); \
        cmake -S . -B "${build_dir}" -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON >/dev/null; \
    elif [ ! -f build/release/compile_commands.json ]; then \
        echo "compile_commands.json manquant dans build/release: configuration automatique..."; \
        just configure >/dev/null; \
        build_dir=build/release; \
    else \
        build_dir=build/release; \
    fi; \
    exclude_header_filter=""; \
    if clang-tidy --help 2>&1 | grep -q -- '--exclude-header-filter'; then \
        exclude_header_filter="--exclude-header-filter=(.*/)?ext/.*"; \
    fi; \
    mapfile -t changed_cpp < <(git diff --name-only --diff-filter=ACMR HEAD -- 'src/*.cpp' 'tests/*.cpp'); \
    mapfile -t changed_headers < <(git diff --name-only --diff-filter=ACMR HEAD -- 'src/*.h' 'tests/*.h'); \
    if [ ${#changed_cpp[@]} -eq 0 ] && [ ${#changed_headers[@]} -eq 0 ]; then \
        echo "Aucun fichier C/C++ modifie: lint-c-changed ignore."; \
        exit 0; \
    fi; \
    if [ ${#changed_headers[@]} -gt 0 ]; then \
        echo "Headers modifies detectes: execution clang-tidy complete (src/tests)."; \
        find src tests -name '*.cpp' -type f | sort | xargs -P `nproc` -I {} clang-tidy -quiet -p "${build_dir}" {} --header-filter='(src/.*|tests/.*)' --warnings-as-errors='*' ${exclude_header_filter}; \
    else \
        printf '%s\0' "${changed_cpp[@]}" | xargs -0 -P `nproc` -I {} clang-tidy -quiet -p "${build_dir}" {} --header-filter='(src/.*|tests/.*)' --warnings-as-errors='*' ${exclude_header_filter}; \
    fi

# Lint CMake.
lint-cmake:
    @echo "Lint CMake..."
    @if command -v cmake-lint >/dev/null 2>&1; then \
        cmake-lint CMakeLists.txt; \
    else \
        uvx --from cmakelang cmake-lint CMakeLists.txt; \
    fi

# Lint shell scripts.
lint-shell:
    @echo "Lint shell scripts..."
    @shellcheck scripts/*.sh

# Lint YAML.
lint-yaml:
    @echo "Lint YAML..."
    @if command -v yamllint >/dev/null 2>&1; then \
        yamllint mkdocs.yml .github/workflows/*.yml .pre-commit-config.yaml; \
    else \
        uvx --from yamllint yamllint mkdocs.yml .github/workflows/*.yml .pre-commit-config.yaml; \
    fi

# Vérifie le format du justfile.
lint-just:
    @echo "Lint justfile..."
    @JUST_UNSTABLE=1 just --fmt --check

# Valide les shaders via glslangValidator.
lint-shaders:
    @echo "Linting des shaders avec glslangValidator..."
    @scripts/compile_shaders.sh lint
    @echo "Linting Shaders terminé."

# Lint la doc Markdown.
lint-docs:
    @echo "Linting du Markdown avec pymarkdown..."
    @if command -v pymarkdownlnt >/dev/null 2>&1; then \
        pymarkdownlnt scan docs/; \
    else \
        uvx pymarkdownlnt scan docs/; \
    fi

# Lint le Dockerfile CI.
lint-dockerfile:
    @echo "Lint Dockerfile..."
    @if command -v hadolint >/dev/null 2>&1; then \
        hadolint --config .hadolint.yaml docker/ci/Dockerfile; \
    else \
        docker run --rm -i -v "${PWD}/.hadolint.yaml:/.hadolint.yaml" hadolint/hadolint < docker/ci/Dockerfile; \
    fi

# Lint les workflows GitHub Actions.
lint-actions:
    @echo "Lint GitHub Actions workflows..."
    @if command -v actionlint >/dev/null 2>&1; then \
        actionlint; \
    else \
        docker run --rm -v "${PWD}:/work" -w /work rhysd/actionlint:latest; \
    fi

# Vérifie qu'aucun type Vulkan ne fuite dans la logique pure.
check-rhi-leaks:
    @echo "Vérification des fuites RHI..."
    @python3 scripts/check_rhi_leaks.py

# Lint Python scripts.
lint-python:
    @echo "Lint Python scripts (ruff)..."
    @uvx ruff check scripts/

# Lint rapide (sans clang-tidy complet ni docker/actions).
lint-fast: check-rhi-leaks lint-cmake lint-shell lint-yaml lint-just lint-shaders lint-docs lint-python

# Lint iteration rapide (inclut clang-tidy sur fichiers modifies).
lint-iter: check-rhi-leaks lint-c-changed lint-cmake lint-shell lint-yaml lint-just lint-shaders lint-docs lint-python

# Lint complet.
lint: lint-c lint-cmake lint-shell lint-yaml lint-just lint-shaders lint-docs lint-dockerfile lint-actions lint-python

# Format + lint + tests (gate local principal).
check: format lint test

# Gate d'iteration rapide pour les boucles dev locales.
check-iter: format lint-iter test-iter

# Compare IBL maps between OpenGL and Vulkan
verify-ibl: build
    @echo "--- 🧹 Cleaning old dumps ---"
    @rm -rf /tmp/ibl_tests
    @mkdir -p /tmp/ibl_tests/ogl /tmp/ibl_tests/vk
    @echo "--- 🎨 Generating OGL Reference ---"
    @cd ../suckless-ogl && cmake -B build && cmake --build build --target test_ibl_extract -j$(nproc) && ./build/tests/test_ibl_extract assets/textures/hdr/abandoned_garage_4k.hdr /tmp/ibl_tests/ogl
    @echo "--- 🌋 Generating Vulkan Results ---"
    @SVK_IBL_DUMP=1 SVK_IBL_HDR=abandoned_garage_4k.hdr just test-all || true
    @echo "--- 📊 Comparing Results ---"
    @uv run scripts/verify_ibl.py

# Build local de l'image Docker CI.
ci-image-build:
    @echo "Build de l'image Docker CI locale..."
    @docker build --network=host -t local/suckless-vulkan-ci:latest -f docker/ci/Dockerfile .

# Exécute le lint dans le conteneur CI.
ci-docker-lint: ci-image-build
    @echo "Lint dans le conteneur CI..."
    @docker run --rm \
        --user "$(id -u):$(id -g)" \
        -e CI=true \
        -e HOME=/tmp \
        -v "$PWD:/work" \
        -w /work \
        local/suckless-vulkan-ci:latest \
        bash -lc "just lint"

# Exécute build + tests dans le conteneur CI pour un type de build donné.
ci-docker build_type="Release": ci-image-build
    @echo "Build+test dans le conteneur CI ({{ build_type }})..."
    @docker run --rm \
        --user "$(id -u):$(id -g)" \
        -e CI=true \
        -e HOME=/tmp \
        -v "$PWD:/work" \
        -w /work \
        local/suckless-vulkan-ci:latest \
        bash -lc "chmod +x scripts/ci/run_ci_build_and_test.sh && scripts/ci/run_ci_build_and_test.sh {{ build_type }}"

# Exécute lint + build/test CI pour Release et Debug.
ci-docker-all: ci-docker-lint
    @just ci-docker Release
    @just ci-docker Debug

# Exécute les tests ASan dans le conteneur CI.
ci-docker-asan: ci-image-build
    @echo "Test ASan dans le conteneur CI (build séparé, leaks SDK informels)..."
    @docker run --rm \
        --user "$(id -u):$(id -g)" \
        -e CI=true \
        -e HOME=/tmp \
        -v "$PWD:/work" \
        -w /work \
        local/suckless-vulkan-ci:latest \
        bash -lc "bash scripts/ci/run_ci_asan.sh"

# Installe les hooks git pre-commit et pre-push.
pre-commit-install:
    @echo "Installation des hooks pre-commit (pre-commit + pre-push)..."
    @uvx pre-commit install --install-hooks --hook-type pre-commit --hook-type pre-push

# Exécute tous les hooks pre-commit sur tous les fichiers.
pre-commit-run:
    @echo "Execution de tous les hooks pre-commit sur le repo..."
    @uvx pre-commit run --all-files

# Exécute les hooks pre-push sur tous les fichiers.
pre-push-run:
    @echo "Execution des hooks de stage pre-push sur le repo..."
    @uvx pre-commit run --hook-stage pre-push --all-files

# Nettoie les artefacts de build et shaders générés.
clean:
    rm -rf build/* shaders/*.spv shaders/*.spvasm *.spv *.spvasm

# Nettoie tout le projet puis le recompile de zéro
rebuild: clean build
    @echo "Rebuild complet terminé avec succès !"

# --- DOCUMENTATION ---

# Installe MkDocs et le thème Material globalement via pipx (Recommandé si tu l'utilises souvent)
docs-install:
    @echo "Installation de mkdocs-material via pipx..."
    uv tool install mkdocs --with mkdocs-material

# Lance le serveur local (hot-reload) si MkDocs est installé
docs-serve:
    @echo "Lancement du serveur de documentation..."
    mkdocs serve

# Génère le site statique final dans le dossier site/
docs-build:
    @echo "Génération de la documentation statique..."
    mkdocs build

# --- ALTERNATIVES UV (Zero-Install) ---

# Lance le serveur de doc à la volée sans installation globale en utilisant uvx
docs-uv-serve:
    @echo "Lancement de MkDocs via uvx (éphémère)..."
    uvx --with mkdocs-material mkdocs serve

# Construit la doc à la volée via uvx
docs-uv-build:
    @echo "Génération statique via uvx..."
    uvx --with mkdocs-material mkdocs build

# Fait les deux d'un coup
check-docs: format-docs lint-docs
    @echo "Documentation propre et validée ! ✨"
