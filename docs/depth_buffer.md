# Gestion de la Profondeur (Depth Buffer)

Le **Depth Buffer** est une image auxiliaire qui stocke la distance (coordonnée Z) de chaque pixel par rapport à la caméra. Il permet au GPU de décider si un nouveau pixel doit être dessiné ou ignoré s'il se trouve derrière un pixel déjà existant.

## 🏗️ Architecture Technique

L'implémentation du Depth Buffer dans notre moteur suit quatre étapes majeures :

### 1. Création de l'Image de Profondeur

Contrairement aux images de la Swapchain qui sont gérées par l'OS, l'image de profondeur est une ressource interne au GPU.

- **Format préféré :** `VK_FORMAT_D32_SFLOAT` (avec fallback automatique vers un autre format depth compatible si nécessaire).
- **Usage :** `VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT`.
- **Mémoire :** Allouée via VMA en mode `VMA_MEMORY_USAGE_GPU_ONLY` pour des performances maximales.

### 2. Modification de la Render Pass

La Render Pass est informée qu'elle doit désormais gérer deux "attachements" :

1. **Color Attachment :** L'image finale affichée à l'écran.
1. **Depth Attachment :** L'image utilisée pour les tests de distance.
   - `loadOp : VK_ATTACHMENT_LOAD_OP_CLEAR` : Le buffer est réinitialisé à 1.0 (distance infinie) au début de chaque frame.

### 3. État du Pipeline (Depth Stencil State)

Le pipeline graphique est configuré pour activer le test :

- **Depth Test :** Activé (`depthTestEnable = VK_TRUE`).
- **Depth Write :** Activé (`depthWriteEnable = VK_TRUE`).
- **Comparaison :** `VK_COMPARE_OP_LESS` (Le pixel est conservé uniquement si sa valeur Z est *inférieure* à la valeur actuelle).

### 4. Synchronisation dans la Frame

À chaque frame (`draw_frame`), nous soumettons deux valeurs de nettoyage (*Clear Values*) :

- Une couleur de fond (Bleu foncé).
- Une valeur de profondeur par défaut (1.0f).

## 🌍 Impact sur l'Icosphère

Grâce au Depth Buffer, nous pouvons désactiver le mode fil de fer et passer en rendu plein (`VK_POLYGON_MODE_FILL`). L'icosphère apparaît alors comme un volume solide et opaque, où la superposition des triangles est gérée mathématiquement par le matériel.
