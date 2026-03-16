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
- **Mathématiques :** La bibliothèque **GLM** est utilisée pour calculer la matrice MVP (Model-View-Projection).

```cpp
// Calcul de la rotation dans draw_frame
glm::mat4 model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
```

## 🏎️ Rendu Indexé

Pour optimiser les performances, nous utilisons `vkCmdDrawIndexed`. Au lieu de dupliquer les sommets partagés entre plusieurs triangles, nous utilisons un **Index Buffer**. Cela réduit l'empreinte mémoire VRAM et améliore l'efficacité du cache de sommets du GPU.
