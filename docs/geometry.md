# Géométrie Procédurale & Uniforms

Plutôt que de charger des fichiers lourds, le moteur génère sa propre géométrie dynamiquement.

## 🔷 L'Icosphère

L'icosphère est générée à partir d'un icosahèdre de base (12 sommets, 20 faces).
Chaque subdivision consiste à :

1. Diviser chaque arête en son milieu.
1. Projeter ce nouveau point sur la sphère unité (Normalisation).
1. Reconstruire 4 triangles à partir d'un seul.

Cette approche garantit une répartition uniforme des sommets, contrairement à une sphère UV classique qui présente des distorsions aux pôles.

## 🔄 Uniform Buffer Objects (UBO)

Pour animer la scène, nous utilisons des matrices de transformation envoyées au GPU via des descripteurs.

- **Pipeline :** Un `DescriptorSetLayout` définit le "contrat" entre le CPU et le Shader.
- **Mémoire :** Un buffer est alloué via VMA avec le flag `VMA_MEMORY_USAGE_CPU_TO_GPU` pour permettre une mise à jour à chaque frame (60Hz+).
- **Mathématiques :** La bibliothèque **GLM** est utilisée pour calculer les matrices de transformation.

Depuis l'ajout de l'instancing, l'UBO contient deux matrices :

```cpp
struct UBOData {
    glm::mat4 vp;            // View * Projection (commune à toutes les instances)
    glm::mat4 modelRotation; // Rotation locale de la sphère (commune à toutes les instances)
};
```

## 🧬 Instancing GPU (10 × 10 Sphères)

Plutôt que d'effectuer 100 draw calls distincts, le moteur utilise l'**instancing Vulkan** pour rendre la grille entière en un seul appel `vkCmdDrawIndexed`.

### Principe

L'instancing repose sur un second vertex buffer lu à fréquence `VK_VERTEX_INPUT_RATE_INSTANCE` : le GPU avance d'un élément dans ce buffer à chaque instance, au lieu de chaque vertex.

```text
Binding 0 (RATE_VERTEX)   → position + color  (données de la sphère)
Binding 1 (RATE_INSTANCE) → instanceOffset    (position monde de chaque sphère)
```

### Instance Buffer

Un `VkBuffer` GPU-only contenant 100 positions `glm::vec3` est généré côté CPU en grille 10 × 10 avec un espacement configurable, puis uploadé via staging buffer au démarrage :

```cpp
const int GRID = 10;
const float SPACING = 2.2f;
for (int row = 0; row < GRID; ++row)
    for (int col = 0; col < GRID; ++col)
        instancePositions[...] = {col * SPACING - offset, row * SPACING - offset, 0.0f};
```

### Vertex Shader

Le shader reçoit `instanceOffset` en `layout(location = 2)` et compose la position finale :

```glsl
void main() {
    vec4 localPos = ubo.modelRotation * vec4(inPosition, 1.0);
    vec4 worldPos = localPos + vec4(instanceOffset, 0.0);
    gl_Position   = ubo.vp * worldPos;
}
```

### Draw Call

```cpp
// Un seul appel pour les 100 instances
vkCmdDrawIndexed(commandBuffer, indexCount, 100, 0, 0, 0);
```

La caméra est reculée (`z = 40`, far plane `100`) pour que toute la grille reste visible.

## 🏎️ Rendu Indexé

Pour optimiser les performances, nous utilisons `vkCmdDrawIndexed`. Au lieu de dupliquer les sommets partagés entre plusieurs triangles, nous utilisons un **Index Buffer**. Cela réduit l'empreinte mémoire VRAM et améliore l'efficacité du cache de sommets du GPU.
