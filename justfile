set shell := ["bash", "-c"]

renderdoc_bin := "qrenderdoc"

# --- CONFIGURATION ---

# Configure CMake (nécessaire pour générer compile_commands.json pour clang-tidy)
configure:
    @cmake -B build/release -S . -DCMAKE_BUILD_TYPE=Release

configure-debug:
    @cmake -B build/debug -S . -DCMAKE_BUILD_TYPE=Debug

# --- COMPILATION ---

shaders:
    @echo "Compilation des shaders..."
    @glslc shaders/shader.vert -o shaders/vert.spv
    @glslc shaders/shader.frag -o shaders/frag.spv
    @echo "Génération de l'assembleur SPIR-V (.spvasm)..."
    @glslc -S shaders/shader.vert -o shaders/vert.spvasm
    @glslc -S shaders/shader.frag -o shaders/frag.spvasm

build: configure shaders
    @echo "Compilation Release..."
    @cmake --build build/release -j$(nproc)

build-debug: configure-debug shaders
    @echo "Compilation Debug..."
    @cmake --build build/debug -j$(nproc)

# --- EXECUTION & DEBUG ---

run: build
    @./build/release/vulkan_app

# Utilisation : just renderdoc_bin=/chemin/vers/qrenderdoc renderdoc
renderdoc: build-debug
    @{{ renderdoc_bin }} --working-dir . ./build/debug/vulkan_app

test: build
    @ctest --test-dir build/release --output-on-failure

# --- NOUVELLES RECETTES DE QUALITÉ DE CODE ---

format-code:
    @echo "Formatage du code C et des Shaders..."
    @clang-format -i src/*.cpp src/*.h tests/*.cpp shaders/*.vert shaders/*.frag
    @echo "Formatage terminé."

format-cmake:
    @echo "Formatage des fichiers CMake..."
    @uvx --from cmakelang cmake-format -i CMakeLists.txt

format-shell:
    @echo "Formatage des scripts shell..."
    @uvx --from shfmt-py shfmt -w scripts/*.sh

format-yaml:
    @echo "Formatage des fichiers YAML..."
    @uvx --from yamlfmt yamlfmt -w mkdocs.yml

format-just:
    @echo "Formatage du justfile..."
    @JUST_UNSTABLE=1 just --fmt

format-docs:
    @echo "Formatage du Markdown avec mdformat..."
    @uvx mdformat docs/

format: format-code format-cmake format-shell format-yaml format-docs format-just

lint-c:
    @if [ ! -f build/release/compile_commands.json ]; then \
    	echo "Base de données de compilation manquante. Génération..."; \
    	cmake -S . -B build/release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON; \
    fi
    clang-tidy -quiet -p build/release src/*.cpp tests/*.cpp --header-filter='(src/.*|tests/.*)' --exclude-header-filter='(.*/)?ext/.*'

lint-cmake:
    @echo "Lint CMake..."
    @uvx --from cmakelang cmake-lint CMakeLists.txt

lint-shell:
    @echo "Lint shell scripts..."
    @shellcheck scripts/*.sh

lint-yaml:
    @echo "Lint YAML..."
    @uvx --from yamllint yamllint mkdocs.yml

lint-just:
    @echo "Lint justfile..."
    @JUST_UNSTABLE=1 just --fmt --check

lint-shaders:
    @echo "Linting des shaders avec glslangValidator..."
    @glslangValidator -V shaders/shader.vert -o /dev/null
    @glslangValidator -V shaders/shader.frag -o /dev/null
    @echo "Linting Shaders terminé."

lint-docs:
    @echo "Linting du Markdown avec pymarkdown..."
    @uvx pymarkdownlnt scan docs/

lint: lint-c lint-cmake lint-shell lint-yaml lint-just lint-shaders lint-docs

check: format lint test

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
