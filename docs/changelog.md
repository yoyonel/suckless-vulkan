# Résumé des Changements Récents

## Implémentations Effectuées (Mars 2026)

### ✨ Nouvelle Fonctionnalité

Une **caméra cinétique complète avec momentum et friction** remplace le système simple basé sur FOV. Ce changement apporte:

- **Inertie physique** : la caméra continue de glisser après l'impulsion
- **Friction progressive** : décélération naturelle (85% conservé par frame)
- **Impulsion scroll** : la molelle ajoute une accélération directe à la vélocité
- **Interpolation WASD** : convergence progressive de la vélocité initiale vers la cible
- **Alignement complet** avec le legacy `suckless-ogl`

### 📝 Fichiers Modifiés

#### `src/camera.h`

- Structure `Camera` augmentée de:
  - `glm::vec3 velocityCurrent` — vélocité courante 3D
  - `float acceleration` — coefficient d'interpolation WASD
  - `float friction` — facteur d'atténuation (0.85)

#### `src/camera.cpp`

- Constants physiques ajoutées:

  ```cpp
  kDefaultAcceleration = 10.0f
  kDefaultFriction = 0.85f
  kDefaultScrollSensitivity = 50.0f
  kMinVelocity = 0.01f
  ```

- `camera_init()` — initialise `velocityCurrent` et paramètres physiques

- `camera_process_scroll()` — **nouveau comportement**:

  ```cpp
  // Avant: modifiait camera->zoom (FOV)
  // Après: ajoute impulsion à velocityCurrent
  glm::vec3 impulse = camera->front * (yoffset * 50.0f);
  camera->velocityCurrent += impulse;
  ```

- `camera_fixed_update()` — **nouvelle logique cinétique**:

  1. Calcule `targetVelocity` depuis flags WASD/QE
  1. Interpole `velocityCurrent` vers `targetVelocity` (accélération=10)
  1. Applique friction (-15% per frame) si pas d'entrée
  1. Intègre position à partir de `velocityCurrent`
  1. Lisse rotation (yaw/pitch) via interpolation

#### `src/main.cpp`

- Message de contrôles mis à jour:

  ```text
  "molette FOV" → "molette impulse cinétique"
  ```

#### `docs/runtime_controls_logging.md`

- Section "Système de Camera Cinétique" mise à jour
- Lien vers [Caméra Cinétique](kinetic_camera.md) pour documentation complète
- Paramètres du legacy documentés pour alignement

### 📄 Nouvelle Documentation

#### `docs/kinetic_camera.md` (420+ lignes)

**Document complet couvrant:**

- Vue d'ensemble et alignement legacy
- Architecture de la structure Camera
- Constantes physiques
- Mécanique cinétique dételle (6 étapes)
- Flux mouvement avec exemples concrets
- Comparaison avec legacy
- Tableaux de contrôles clavier
- Instructions pour ajuster paramètres
- Intégration au moteur
- Tests associés

**Longueur** : Suffisamment de détails pour implémenter des variations sans recompiler.

### ✅ Validation

```text
just format  ✅ Passes (clang-format)
just lint    ✅ Passes (clang-tidy, 0 errors, 0 warnings)
just test-all ✅ Passes (EngineIntegrationTest + LogicTests)
```

### 🔄 Intégration mkdocs.yml

Le nouveau document est ajouté à la navigation:

```yaml
nav:
  - Accueil: index.md
  - Architecture du Moteur: architecture.md
  - Gestion de la Mémoire (VMA): memory.md
  - Géométrie & Uniforms: geometry.md
  - Gestion du Depth Buffer: depth_buffer.md
  - Optimisation VRAM (Staging): vram_optimization.md
  - Caméra Cinétique: kinetic_camera.md  ← NOUVEAU
  - Runtime Controls et Logging: runtime_controls_logging.md
  - Pipeline Envmap HDR: envmap_hdr_pipeline.md
  - ...
```

______________________________________________________________________

## Phase 2 : HDR Catalogue et Runtime Switching (Mars 2026)

### 🎨 Nouvelle Fonctionnalité (HDR Catalogue)

**Catalogue d'envmap HDR avec changement runtime** remplace l'envmap unique. C'est le début de la stratégie async:

- Scanne tous les fichiers `.hdr` dans `assets/textures/hdr`
- Navigation via `PageUp/PageDown` (circular)
- Rechargement synchrone avec mise à jour descriptor
- Remapping des touches LOD à `Shift+PageUp/PageDown`

### 📄 Documentation Existante

**Voir [Pipeline Envmap HDR](envmap_hdr_pipeline.md):**

- Technique actuelle (fullscreen skybox, invViewProj)
- Pipeline de chargement (catalog + sync reload)
- Fallback CI (1x1 black texture)
- Limitations (sync hitch, pas de transition visuelle)
- **Roadmap 5 phases** (A: Catalogue, B: Async, C: GPU upload, D: Memory, E: IBL)
- Phase A marquée "Partially implemented"

______________________________________________________________________

## Phase 1 : Alignement Legacy (Mars 2026)

### ✨ Alignement des Paramètres

Tous les paramètres caméra et scène alignés avec `suckless-ogl` pour parity visuelle:

**Caméra:**

- Distance initiale: `20.0f` (au lieu de 40)
- Vitesse mouvement: `15.0f` (au lieu de 12)
- Sensibilité souris: `0.15f` (au lieu de 0.12)
- Rotation smoothing: `0.18f` (au lieu de 0.2)
- FOV skybox: `60°` (au lieu de 45°)
- FOV pour projections: `60°` (au lieu de 45°)

**Scène:**

- Grid spacing: `2.5f` (au lieu de 4.0f)
- Near plane: `0.1f` (au lieu de variable)
- Far plane: `1000.0f` (au lieu de 100.0f)

### 📝 Fichiers Affectés

- `src/camera.cpp` — Constants mises à jour
- `src/vk_engine.cpp` — Grid spacing, projection matrices
- `src/vk_engine.h` — Constants, projection planes

______________________________________________________________________

## Résumé des Changements Cumulatifs

| Phase | Fonctionnalité | Documentation | Validation |
|-------|---|---|---|
| 1 | Alignement legacy (paramètres) | `kinetic_camera.md` (tableau) | ✅ Tests pass |
| 2 | HDR catalogue + runtime switch | `envmap_hdr_pipeline.md` | ✅ Tests pass |
| 3 | Caméra cinétique complète | `kinetic_camera.md` (complet) | ✅ Format, Lint, Tests |

______________________________________________________________________

## Prochaines Étapes Possibles

### Phase 2B : Async HDR Loading

- Thread I/O pour décodage off-render
- Queue-based request system
- Suppression du sync hitch
- **Documentation** : Section dans `envmap_hdr_pipeline.md`

### Phase 2C : Async GPU Upload

- Ring buffers + timeline semaphore
- Non-blocking GPU transfer
- **Documentation** : Extension `envmap_hdr_pipeline.md`

### UX Enhancements

- Affichage metadata envmap (résolution, taille)
- Transition visuelle (fade/crossfade)
- UI dropdown pour sélection
- **Documentation** : Nouveau `docs/envmap_ui.md`

______________________________________________________________________

## Commandes de Validation

```bash
# Format et validation
just format
just lint

# Tests complets
just test-all

# Serveur documentation local
just docs-serve
# → http://localhost:8000
```

______________________________________________________________________

## État Git

**Branches actives:**

- `dev` — développement continu

**À committer (si non encore committé):**

```bash
M src/camera.cpp           # Logique cinétique
M src/camera.h             # Structure augmentée
M src/vk_engine.cpp        # (Phase 1/2 alignment)
M src/vk_engine.h          # Constants (Phase 1/2)
M src/main.cpp             # Controls message
M src/runtime_controls.h   # (Legacy)
M docs/kinetic_camera.md   # NOUVEAU
M docs/runtime_controls_logging.md  # Mise à jour
M docs/envmap_hdr_pipeline.md       # (Phase 2)
M mkdocs.yml               # Add kinetic_camera.md
M README.md                # (si nécessaire)
```

______________________________________________________________________

## Contact & Questions

Pour détails techniques:

- **Caméra cinétique** → [docs/kinetic_camera.md](kinetic_camera.md)
- **HDR pipeline** → [docs/envmap_hdr_pipeline.md](envmap_hdr_pipeline.md)
- **Contrôles runtime** → [docs/runtime_controls_logging.md](runtime_controls_logging.md)
- **Architecture moteur** → [docs/architecture.md](architecture.md)
