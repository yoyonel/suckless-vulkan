# Audit & Architecture du Pipeline de Rendu : Legacy SDR vs Nouveau HDR

- **Date** : 16 Août 2026
- **Auteur** : Antigravity (Google DeepMind)
- **Contexte** : Intégration du Post-Processing Bloom & Parité ISO `suckless-ogl`
- **Branche** : `feature/render-graph-integration`
- **Statut** : 📄 **Document de Référence & État des Lieux Technique**

______________________________________________________________________

## 1. Synthèse Exécutive

Lors de l'implémentation de l'effet post-process **Bloom Dual-Filtering (Jimenez / Karis)**, une divergence structurelle majeure a été identifiée entre l'architecture historique héritée (*Legacy SDR 8-bit*) et le standard attendu pour un pipeline moderne HDR physique (*ISO suckless-ogl / Unreal Engine*).

Le présent document :

1. Formalise les différences mathématiques et architecturales entre les deux pipelines.
1. Explique pourquoi les Golden Images de référence ont montré des variations lors de la transition.
1. Dresse un inventaire exhaustif de toutes les incohérences SDR vs HDR résiduelles identifiées dans la base de code `suckless-vulkan`.

______________________________________________________________________

## 2. Comparaison Architecturale des Pipelines

```text
================================================================================
PIPELINE HISTORIQUE : LEGACY SDR (8-bit Fixed Point)
================================================================================

 [Forward Pass]
   ├─ Evaluation PBR & Skybox
   ├─ Exposure (color *= ubo.exposure)
   ├─ Color Grading & White Balance
   └─ unrealTonemap() [PER-FRAGMENT] ──► Clamping forcé [0.0, 1.0]
                                               │
                                               ▼
                              colorAttachment (BGRA8_UNORM / 8-bit SDR)
                                               │
                                               ▼
 [Bloom Compute Pass] ──► INOPÉRANT (Toutes valeurs > 1.0 écrasées à 1.0)
                                               │
                                               ▼
 [PostProcess Pass]   ──► Pass-Through simple vers Swapchain (SRGB)


================================================================================
NOUVEAU PIPELINE : HDR PHYSIQUE (16-bit Float Linear - ISO suckless-ogl)
================================================================================

 [Forward Pass]
   ├─ Evaluation PBR & Skybox (Radiance pure, non bornée [0.0, +inf[)
   └─ Sortie Linéaire HDR
                                               │
                                               ▼
                              colorAttachment (RGBA16_SFLOAT / 16-bit HDR)
                                               │
                                               ▼
 [Bloom Dual-Filtering Pass] (9 dispatches compute)
   ├─ Downsample 0 (Prefilter Soft-Knee + Karis Weighting sur valeurs > 1.0)
   ├─ Downsamples 1..4 (13-Tap Jimenez Tent Filter)
   └─ Upsamples 3..0 (9-Tap 3x3 Tent Filter avec mélange additif)
                                               │
                                               ▼ Texture Bloom HDR (540p)
 [PostProcess Pass] (postprocess.frag)
   ├─ 1. Echantillonnage Scène HDR (u_scene_hdr)
   ├─ 2. Debug View (si bloomDebugMode != 0, affiche bloom isolé)
   ├─ 3. Composition Additive : scene.rgb + bloom.rgb * bloomIntensity
   ├─ 4. Exposure & White Balance
   ├─ 5. Color Grading (Saturation, Contraste, Gamma, Gain, Offset)
   ├─ 6. Tonemapping (unrealTonemap HDR → LDR)
   └─ 7. Ecriture vers Swapchain (VK_FORMAT_B8G8R8A8_SRGB avec conversion gamma HW)
```

______________________________________________________________________

## 3. Analyse Détaillée des Différences & Impacts sur les Golden Images

### 3.1. Précision Mathématique et Résolution des Dégradés

- **Legacy 8-bit** : Les calculs d'antialiasing analytique des sphères billboards (`billboard.frag`) et les dégradés d'ombrage étaient quantifiés sur 256 niveaux par canal $[0..255]$ immédiatement après le tonemapping per-fragment.
- **HDR 16-bit Float** : L'interpolation géométrique, l'antialiasing sub-pixel et le blending multi-échantillons sont effectués avec une mantisse flottante 16-bit (65536 niveaux et plage dynamique $> 65000$). Le tonemapping n'intervenant qu'à la toute fin en PostProcess, la distribution lumineuse sur les bords lissés présente une fidélité sub-pixel supérieure, expliquant les écarts de RMSE observés avec les anciennes références 8-bit.

### 3.2. Préservation de l'Énergie Lumineuse

- Dans l'ancien pipeline, le ciel extérieur de la skybox `abandoned_garage_4k.hdr` (qui culmine à une luminance $> 20.0$) était clampé à $1.0$ dès le premier pixel tracé.
- Dans le nouveau pipeline HDR, cette énergie traverse intacte le `colorAttachment` en 16F, permettant au filtre Karis et au Soft-Knee du Bloom de générer le halo d'éblouissement naturel et aux icosphères métalliques de refléter cette haute dynamique.

______________________________________________________________________

## 4. Inventaire des Incohérences SDR vs HDR Détectées dans le Codebase

Un audit approfondi ligne par ligne a mis en évidence **5 incohérences** à harmoniser :

### Incohérence 1 : Emplacement d'Exposure et de Color Grading

- **Fichiers concernés** : `shaders/shader.frag` (L210), `shaders/skybox.frag` (L98), `shaders/billboard.frag` (L178).
- **Problème** : L'exposition (`color *= ubo.postParams1.x`) et le `applyColorGrading` sont exécutés dans les shaders de géométrie forward avant l'écriture dans `colorAttachment`.
- **Impact** :
  1. Le calcul de contraste non-linéaire `(color - 0.5) * contrast + 0.5` s'exécute sur des valeurs HDR non bornées.
  1. Si l'utilisateur modifie l'exposition, cela modifie artificiellement l'énergie brute transmise au Bloom avant le seuil (threshold), au lieu de laisser le Bloom extraire l'énergie physique de la scène.
- **Correction recommandée (ISO suckless-ogl)** : Laisser le Forward Pass sortir une radiance linéaire pure $(1.0 \\times \\text{Albedo} \\times \\text{Lighting})$, et déplacer l'exposition et le color grading dans `shaders/postprocess.frag` après l'addition du Bloom.

______________________________________________________________________

### Incohérence 2 : Formats des Textures Transitoires dans le RenderGraph

- **Fichier concerné** : `src/vk_engine_frame.cpp` (L494-L498).
- **Problème** : Les descripteurs d'images transitoires de démonstration pour le GBuffer et le SSAO sont tous typés en `VK_FORMAT_R8G8B8A8_UNORM`.
- **Impact** : Inadéquat pour un stockage de normales de précision ou de passes d'accumulation HDR en deferred.
- **Correction recommandée** : Utiliser `VK_FORMAT_R16G16B16A16_SFLOAT` pour les buffers de radiance/normales et `VK_FORMAT_R8_UNORM` pour les masques d'occlusion (SSAO).

______________________________________________________________________

### Incohérence 3 : Gestion de la Correction Gamma (HW sRGB vs Shader)

- **Fichiers concernés** : `src/swapchain_init.cpp` (L11), `shaders/postprocess.frag`.
- **Problème** : Dans `suckless-ogl`, le framebuffer final est UNORM, donc `postprocess.frag` applique `pow(color, vec3(1.0 / 2.2))`. Dans `suckless-vulkan`, la surface swapchain sélectionnée par défaut est `VK_FORMAT_B8G8R8A8_SRGB`.
- **Impact** : Le GPU Vulkan effectue la conversion linéaire $\\rightarrow$ sRGB automatiquement lors de l'écriture dans l'image de présentation. Il ne faut **pas** ajouter de `pow(color, 1.0/2.2)` explicite dans `postprocess.frag`, sous peine d'appliquer deux fois la courbe gamma (scène surexposée et délavée).

______________________________________________________________________

### Incohérence 4 : Pipeline Subpass Fusion vs Multi-Pass

- **Fichiers concernés** : `src/vk_engine_init.cpp` (L449-L558), `src/vk_engine_frame.cpp` (L509-L514).
- **Problème** : Le mode historique `useSubpassFusion` créait un RenderPass à 2 subpasses monolithique avec 3 attachements (`colorAttachment`, `depthImage`, `swapchainImage`). Ce mode empêche tout dispatch compute intermédiaire (comme le Bloom) et force une écriture sur puce sans mémoire partagée pour les mips.
- **Statut** : `useSubpassFusion` est désormais désactivé par défaut (`false`) pour opérer en mode multi-pass RenderGraph standard, tout en étant conservé dans un test unitaire dédié isolé (`test_subpass_fusion_standalone`).

______________________________________________________________________

### Incohérence 5 : Sampling Bilinéaire du Sampler de Post-Process

- **Fichiers concernés** : `src/vk_engine_init.cpp` (L1007).
- **Problème** : `engine->postProcessSampler` est configuré en `VK_FILTER_LINEAR` avec `CLAMP_TO_EDGE`.
- **Impact** : Le filtrage linéaire sur une texture 16F HDR lors du blit final plein écran est optimal, mais requiert que le GPU supporte `VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT` sur `VK_FORMAT_R16G16B16A16_SFLOAT` (supporté universellement sur tous les GPU Vulkan 1.2+ desktop et mobile modernes).

______________________________________________________________________

## 5. Plan d'Action Recommandé

| Étape | Action | Bénéfice |
| :--- | :--- | :--- |
| **Étape 1** | **Consolidation PostProcess** : Déplacer `Exposure` et `Color Grading` de `shader.frag`/`skybox.frag` vers `postprocess.frag`. | Pipeline physique pur 100% ISO `suckless-ogl`, zéro dérive colorimétrique du Bloom lors des changements d'exposition. |
| **Étape 2** | **Verrouillage des Références** : Valider et figer définitivement les Golden Images générées par le pipeline HDR unifié. | Base de tests stable, imperméable aux régressions visuelles. |
| **Étape 3** | **Phase 4 (Levier 5 VMA Aliasing)** : Mutualiser la mémoire des mips de Bloom pour réduire la consommation VRAM sous les $3.5\\text{ MB}$. | Optimisation mémoire sans aucun impact sur le rendu. |
