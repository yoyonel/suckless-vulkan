# Caméra Cinétique avec Momentum et Friction

## Vue d'ensemble

Le système de caméra implémente une **physique simplifiée avec momentum et friction** pour offrir une sensation naturelle et fluide de mouvement en 3D. Contrairement à un système classique où le mouvement est instantané, ce système simule l'inertie physique.

### Alignement Legacy

La caméra est entièrement alignée avec le système du legacy `suckless-ogl`:

- **Distance de caméra** : 20.0m (par défaut)
- **Vitesse de mouvement** : 15.0 unités/sec
- **Sensibilité souris** : 0.15
- **Rotation smooth** : 0.18 (lerp factor)
- **FOV skybox** : 60° (fixe)
- **Plans de projection** : near=0.1, far=1000.0

______________________________________________________________________

## Architecture de la Caméra

### Structure `Camera` (src/camera.h)

```cpp
struct Camera {
    // Position et orientation
    glm::vec3 position;
    glm::vec3 front;
    glm::vec3 up;
    glm::vec3 right;
    glm::vec3 worldUp;

    float yaw;      // Rotation horizontale (-180 à 180)
    float pitch;    // Rotation verticale (-89 à +89)
    float zoom;     // FOV skybox uniquement (60° fixe)

    // Mouvement cinétique et rotation smooth
    glm::vec3 velocityCurrent;    // Vélocité 3D courante (momentum)
    float acceleration;            // Coefficient d'interpolation WASD → targetVelocity
    float friction;                // Atténuation par frame (0.85 = 85% conservé)
    float movementSpeed;           // Vitesse max (15.0 units/sec)
    float mouseSensitivity;        // Facteur rotation souris (0.15)
    float rotationSmoothing;       // Interpolation yaw/pitch (0.18)

    // États de mouvement
    bool moveForward, moveBackward, moveLeft, moveRight, moveUp, moveDown;

    // Souris
    double lastMouseX, lastMouseY;
    bool firstMouse;
};
```

### Constantes Physiques (src/camera.cpp)

```cpp
kDefaultDistance = 20.0f                // Distance initiale caméra-scène
kDefaultSpeed = 15.0f                   // Vitesse max mouvement (units/sec)
kDefaultSensitivity = 0.15f             // Sensibilité rotation souris
kDefaultZoom = 60.0f                    // FOV skybox (fixe)
kDefaultRotationSmoothing = 0.18f       // Interpolation rotation
kDefaultAcceleration = 10.0f            // Intensité d'accélération WASD
kDefaultFriction = 0.85f                // Facteur atténuation (85% conservé)
kDefaultScrollSensitivity = 50.0f       // Force impulsion scroll
kMinVelocity = 0.01f                    // Seuil d'arrêt complet
```

______________________________________________________________________

## Mécanique Cinétique Complète

### 1. État des Touches (WASD/QE/Souris)

Les touches définissent des **flags booléens**, pas une vélocité directe:

```cpp
On keypress: camera->moveForward = true
On keyrelease: camera->moveForward = false
```

### 2. Calcul de la Vélocité Cible

À chaque frame, depuis les flags, on construit `targetVelocity` (direction + magnitude max):

```cpp
glm::vec3 targetVelocity(0, 0, 0);
if (camera->moveForward)   targetVelocity += front * speed;
if (camera->moveBackward)  targetVelocity -= front * speed;
if (camera->moveLeft)      targetVelocity -= right * speed;
if (camera->moveRight)     targetVelocity += right * speed;
if (camera->moveUp)        targetVelocity += worldUp * speed;
if (camera->moveDown)      targetVelocity -= worldUp * speed;
```

**Résultat** : `targetVelocity` = direction à vitesse max, ou zéro si aucune touche.

### 3. Impulsion par Scroll

La **molette souris ajoute directement une impulsion** à la vélocité courante (indépendant des touches):

```cpp
void camera_process_scroll(Camera* camera, float yoffset) {
    glm::vec3 impulse = camera->front * (yoffset * kDefaultScrollSensitivity);
    camera->velocityCurrent += impulse;
}
```

**Magnitude impulsion** : `yoffset * 50.0` (ex: 1 clic = +50 unités/sec dans la direction front).

### 4. Interpolation vers la Vélocité Cible

La `velocityCurrent` est **progressivement interpolée** vers `targetVelocity` avec une **accélération basée sur deltaTime**:

```cpp
float alpha = std::min(camera->acceleration * deltaSeconds, 1.0f);
camera->velocityCurrent = glm::mix(camera->velocityCurrent, targetVelocity, alpha);
```

**Dynamique** :

- Plus `acceleration` est grand, plus rapide la transition.
- `acceleration = 10.0` → atteint ~95% en ~0.3 secondes.
- La vélocité change graduellement, même en relachant une touche.

### 5. Application de la Friction

Quand **aucune touche de mouvement n'est pressée** (`targetVelocity ≈ 0`), la vélocité est **atténuée par friction**:

```cpp
float targetMagnitude = glm::length(targetVelocity);
if (targetMagnitude < kMinVelocity) {  // Pas de touche pressée
    camera->velocityCurrent *= camera->friction;  // Multiplier par 0.85
}
```

**Effet** : La caméra continue à glisser et ralentit graduellement (non pas s'arrête net).

### 6. Intégration Position

La position est mise à jour par la vélocité courante et le deltaTime:

```cpp
camera->position += camera->velocityCurrent * deltaSeconds;
```

______________________________________________________________________

## Flux de Mouvement : Exemples Concrets

### Exemple 1 : Mouvement Simple (WASD → Accélération Lisse)

```text
Temps 0.00s : Appui W
              targetVelocity = front * 15.0
              velocityCurrent = 0 → 15 (interpolation progressive)

Temps 0.10s : Toujours W appuyé
              targetVelocity = front * 15.0
              velocityCurrent ≈ 14.0 (presque atteint 15)

Temps 0.20s : Relâche W
              targetVelocity = 0
              velocityCurrent ≈ 14.0 → décélère lentement via friction
              Magnitude > kMinVelocity → velocityCurrent *= 0.85

Temps 0.30s : Aucune entrée
              velocityCurrent ≈ 10.0 (continuera à glisser, puis ralentir)
              
Temps 1.00s : Après friction répétée
              velocityCurrent ≈ 0.001 (threshold atteint, arrêt complet)
```

**Sensation** : Accélération rapide, décélération progressive et naturelle.

______________________________________________________________________

### Exemple 2 : Impulsion au Scroll (Inertie Cinétique)

```text
Temps 0.00s : Caméra immobile, scroll UP (yoffset = 1)
              Impulsion = front * 50.0
              velocityCurrent = 0 + impulse → 50.0

Temps 0.05s : Aucune touche, scroll décélère via friction
              velocityCurrent ≈ 42.5 (50 * 0.85)
              Camera.position avance rapidement

Temps 0.20s : Friction continue
              velocityCurrent ≈ 6.0
              Camera ralentit graduellement

Temps 0.50s : Friction répétée
              velocityCurrent ≈ 0.0 (arrêt complet)
```

**Sensation** : "Jet d'impulsion" réaliste avec inertie.

______________________________________________________________________

### Exemple 3 : Scroll + WASD Contemporain

```text
Temps 0.00s : WASD appuyé + Scroll UP
              targetVelocity = front * 15.0  (WASD)
              velocityCurrent += front * 50  (Scroll)
              velocityCurrent ≈ 50.0 (impulsion dominante)

Temps 0.15s : Toujours WASD + plus de scroll
              velocityCurrent → targetVelocity (15.0) via interpolation
              velocityCurrent ≈ 20.0 (en approche)

Temps 0.30s : Relâche tout
              targetVelocity = 0
              velocityCurrent → 0, puis multiplié par friction
              Ralentir graduel
```

**Sensation** : Accélération par scroll, puis convergence vers WASD, puis freinage naturel.

______________________________________________________________________

## Rotation Lisse (Yaw/Pitch)

### Souris + Smoothing

La rotation **souris cible** est progressivement interpolée:

```cpp
void camera_process_mouse(Camera* camera, float xoffset, float yoffset) {
    camera->yawTarget += xoffset * camera->mouseSensitivity;
    camera->pitchTarget -= yoffset * camera->mouseSensitivity;
    camera->pitchTarget = std::clamp(camera->pitchTarget, kPitchMin, kPitchMax);
}

void camera_fixed_update(Camera* camera, float deltaSeconds) {
    // ...
    const float rotAlpha = std::clamp(camera->rotationSmoothing, 0.0f, 1.0f);
    camera->yaw += (camera->yawTarget - camera->yaw) * rotAlpha;
    camera->pitch += (camera->pitchTarget - camera->pitch) * rotAlpha;
    camera_update_vectors(camera);
}
```

**Effet** : La rotation suit la souris, mais avec **lag simulé** (smoothing = 0.18).

**Note** : Contrairement au mouvement (qui utilise momentum complet), la rotation est juste interpolée linéairement.

______________________________________________________________________

## Contrôles Clavier

### Mouvement et Impulsion

| Touche | Action |
|--------|--------|
| `W` | Avancer (targetVelocity step: 15) |
| `S` | Reculer (targetVelocity step: -15) |
| `A` | Strafing gauche (targetVelocity step: -15) |
| `D` | Strafing droit (targetVelocity step: 15) |
| `Q` | Descendre (targetVelocity step: -15) |
| `E` | Monter (targetVelocity step: 15) |
| `Molette ↑` | Impulsion avant (impulse magnitude: 50) |
| `Molette ↓` | Impulsion arrière (impulse magnitude: -50) |

### Animation et Skybox

| Touche | Action |
|--------|--------|
| `Espace` | Pause/Reprise animation |
| `↑` | Accélération animation (×1.25) |
| `↓` | Ralentissement animation (×0.8) |
| `R` | Reset temps animation |
| `K` | Affiche/Cache skybox HDR |

### Changement d'Envmap HDR

| Touche | Action |
|--------|--------|
| `PageUp` | Envmap HDR suivante |
| `PageDown` | Envmap HDR précédente |
| `Shift + PageUp` | Augmente LOD sampling |
| `Shift + PageDown` | Diminue LOD sampling |

### Système

| Touche | Action |
|--------|--------|
| `C` | Active/Désactive capture souris |
| `F11` | Fullscreen/Fenêtre |
| `Esc` | Quitter |

______________________________________________________________________

## Comparaison avec Legacy (suckless-ogl)

### Avant (Legacy)

```text
Scroll → modifie zoom FOV (changement direct)
         (Non-cinétique, change projection)
```

### Après (Vulkan Actuel)

```text
Scroll → ajoute impulsion à velocityCurrent (cinétique)
         (Physics-based, inertie et friction)
```

### Paramètres Alignés

| Paramètre | Legacy | Vulkan |
|-----------|--------|--------|
| Distance initiale | 20.0f | 20.0f ✓ |
| Vitesse mouvement | 15.0f | 15.0f ✓ |
| Sensibilité souris | 0.15f | 0.15f ✓ |
| FOV skybox | 60° | 60° ✓ |
| Near plane | 0.1f | 0.1f ✓ |
| Far plane | 1000.0f | 1000.0f ✓ |
| Rotation smooth | 0.18f | 0.18f ✓ |
| Accélération WASD | 10.0f | 10.0f ✓ |
| Friction | 0.85f | 0.85f ✓ |
| Scroll sensitivity | 50.0f | 50.0f ✓ |

**Résultat** : Caméra cinétique Vulkan offre la même **sensation et paramètres** que legacy, avec système physique amélioré.

______________________________________________________________________

## Ajustement des Paramètres

Pour modifier le comportement cinétique, modifier les constantes dans `src/camera.cpp`:

```cpp
// Plus haute = accélération plus rapide (appui WASD → vitesse max plus vite)
constexpr float kDefaultAcceleration = 10.0f;

// Plus proche de 1.0 = moins de friction (glisse plus longtemps)
// Plus proche de 0.0 = plus de friction (s'arrête rapidement)
constexpr float kDefaultFriction = 0.85f;

// Plus haute = impulsion scrolls plus forte (molette = accélération plus grande)
constexpr float kDefaultScrollSensitivity = 50.0f;

// Seuil pour activer la friction (si ||targetVelocity|| < kMinVelocity)
constexpr float kMinVelocity = 0.01f;
```

______________________________________________________________________

## Implémentation Détachée des Touches

Pour modifier les bindings clavier, voir `src/vk_engine_runtime.cpp` (`vk_handle_runtime_input()` et callbacks GLFW).

La séparation entre:

- **Logique caméra** : `src/camera.cpp` (physique pure)
- **Gestion touches** : `src/vk_engine_runtime.cpp` (I/O GLFW -> flags)

...permet d'ajuster contrôles sans toucher à la physique.

______________________________________________________________________

## Intégration au Moteur

### Appel par Frame

```cpp
void draw_frame(VulkanEngine* engine) {
    // 1. Traiter les touches → met à jour flags
    vk_handle_runtime_input(engine);
    
    // 2. Mettre à jour caméra avec physics cinétique
    camera_fixed_update(&engine->camera, engine->lastFrameDeltaSeconds);
    
    // 3. Utiliser position/orientation pour view matrix
    glm::mat4 view = glm::lookAt(
        engine->camera.position,
        engine->camera.position + engine->camera.front,
        engine->camera.up
    );
    
    // ... renderpass ...
}
```

### Callbacks GLFW

```cpp
static void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    vk_scroll_callback(window, xoffset, yoffset);
}

static void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    vk_mouse_callback(window, xpos, ypos);
}
```

______________________________________________________________________

## Tests Associés

- **Unit tests** : `tests/test_main.cpp` (validation logique caméra)
- **Integration tests** : `tests/test_logic.cpp` (validation dans moteur)
- **Validation runtime** : `just test-all`

______________________________________________________________________

## Ressources

- [src/camera.h](../src/camera.h) — Défnition structure
- [src/camera.cpp](../src/camera.cpp) — Implémentation physique
- [src/vk_engine_runtime.cpp](../src/vk_engine_runtime.cpp) — Input runtime + callbacks GLFW
- [src/vk_engine_frame.cpp](../src/vk_engine_frame.cpp) — Intégration par frame
- [docs/runtime_controls_logging.md](runtime_controls_logging.md) — Contrôles runtime et logging
