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

## 🧬 Instancing GPU (Flat Transform SSBO)

Plutôt que d'effectuer 100 draw calls distincts, le moteur utilise l'**instancing Vulkan** pour rendre la grille entière en un seul appel `vkCmdDrawIndexed`.
Cependant, l'approche traditionnelle basée sur les `vkCmdBindVertexBuffers` par instance a été remplacée par un paradigme Data-Oriented et **GPU-Driven**.

### Principe du Flat Transform Buffer

L'instancing ne repose plus sur un Vertex Buffer lu avec `VK_VERTEX_INPUT_RATE_INSTANCE`.
L'architecture a été migrée vers un **Storage Buffer (SSBO)** qui contient directement les matrices `glm::mat4` pré-calculées pour chaque instance. Le vertex shader va "pull" ses données au vol :

```text
Binding 0 (RATE_VERTEX) → position + color  (données de la sphère)
Binding 9 (SSBO)        → mat4 transforms[] (matrices globales)
```

### Le Storage Buffer

Un `VkBuffer` VRAM-only mappé via `VMA_MEMORY_USAGE_CPU_TO_GPU` (le **Flat Transform Buffer**) est mis à jour à chaque frame côté CPU :

```cpp
if (engine->transformBufferMapped && core.instancePositions) {
    glm::mat4* transforms = static_cast<glm::mat4*>(engine->transformBufferMapped);
    for (uint32_t i = 0; i < core.instanceCount; ++i) {
        transforms[i] = glm::translate(glm::mat4(1.0f), core.instancePositions[i]) * uboData.modelRotation;
    }
}
```

Ce modèle DOD (Data-Oriented Design) avec boucle vectorisable améliore drastiquement l'efficacité du Cache L1 CPU, avec **une réduction de 10.6% des loads mémoire (1.74B → 1.55B)** sur nos benchmarks.

### Vertex Shader

Le shader ne reçoit plus d'attribut par instance (`location = 2`) mais accède au SSBO via le `gl_InstanceIndex` natif de Vulkan :

```glsl
layout(std430, set = 0, binding = 9) readonly buffer TransformBuffer {
    mat4 transforms[];
};

void main() {
    mat4 modelMat = transforms[gl_InstanceIndex];
    vec4 worldPos = modelMat * vec4(inPosition, 1.0);
    gl_Position   = ubo.vp * worldPos;
    outWorldPos   = worldPos.xyz;
    outNormal     = normalize(mat3(modelMat) * inPosition);
}
```

### Draw Call

```cpp
// Un seul appel pour les 100 instances sans bind l'Instance VBO !
vkCmdDrawIndexed(commandBuffer, indexCount, 100, 0, 0, 0);
```

La caméra est reculée (`z = 40`, far plane `100`) pour que toute la grille reste visible.

## 🏎️ Rendu Indexé

Pour optimiser les performances, nous utilisons `vkCmdDrawIndexed`. Au lieu de dupliquer les sommets partagés entre plusieurs triangles, nous utilisons un **Index Buffer**. Cela réduit l'empreinte mémoire VRAM et améliore l'efficacité du cache de sommets du GPU.
