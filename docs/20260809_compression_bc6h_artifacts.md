# Analyse : Compression GPU Native BC6H (2026-08-09)

Ce document détaille les impacts techniques et visuels d'une potentielle transition vers le format compressé `BC6H` (Option A de l'optimisation VRAM), et explique pourquoi cette piste a été mise en pause au profit de la fidélité visuelle.

## 1. Rappel du Concept

Le format `BC6H` est une compression par blocs (4x4 pixels) avec perte (lossy) supportée nativement par les GPU, conçue spécifiquement pour les images HDR (flottants 16-bits).
Le passage d'un format `RGBA32F` (non compressé) au `BC6H` permet de réduire la consommation VRAM d'un facteur 6 à 8 (Ex: 134 Mo -> ~22 Mo pour une map 4K).

## 2. Impact Visuel et Artefacts

Bien que très efficace sur la mémoire, l'utilisation de `BC6H` sur nos textures environnementales (HDR / IBL) introduirait des dégradations :

### A. Skybox et Dégradés (Banding)

Le ciel présente souvent de grands dégradés de couleurs subtils. La compression par blocs 4x4 de `BC6H` a tendance à mal gérer les pentes douces de gradients, introduisant du **banding** (effets d'escalier de couleurs) ou des **artefacts de macroblocs** très visibles.

### B. Précision HDR et "Energy Loss"

`BC6H` utilise des flottants 16-bits. Certaines zones extrêmement lumineuses d'une image HDR (ex: le soleil) peuvent générer des valeurs supérieures à la limite des flottants signés/non-signés 16-bits (~65500). L'algorithme va soit cliper, soit lisser ces pics d'intensité. Résultat : l'énergie globale perçue de la scène peut être légèrement diminuée.

### C. Calculs IBL (Image Based Lighting)

Le pipeline de rendu actuel utilise massivement l'IBL pour l'éclairage de nos sphères raytracées.

- **Irradiance (Diffuse IBL) : Impact Nul.** L'irradiance est calculée via une convolution intégrale (un flou massif sur un hémisphère). Les micro-artefacts de compression sont parfaitement noyés et moyennés lors de cette passe.
- **Prefiltered Map (Specular IBL) : Impact Modéré.**
  - Pour des matériaux rugueux (roughness élevé), les reflets sont diffus, cachant les artefacts.
  - Pour des matériaux lisses (métaux, miroirs purs avec roughness ~0.0), la texture d'environnement est samplée à son LOD 0. Les artefacts de compression par blocs 4x4 deviendraient alors visibles directement à la surface des sphères raytracées.

## 3. Conclusion

Dans le contexte actuel du projet (démonstration technique visuelle mettant en valeur le rendu raytracé PBR exact), la qualité d'image absolue prime sur l'économie de quelques dizaines de Mo de VRAM.

L'Option A (`BC6H`) reste valide et documentée si le projet venait à évoluer vers un jeu complet nécessitant le chargement de centaines de matériaux ou textures HDR simultanées. En l'état, elle est considérée comme introduisant trop d'artefacts indésirables pour notre cas d'usage précis.
