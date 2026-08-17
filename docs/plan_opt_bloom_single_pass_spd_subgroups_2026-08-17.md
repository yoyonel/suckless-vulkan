# Plan d'Implémentation : Bloom Single-Pass Downsampler (AMD SPD & Subgroup Compute)

- **Date de création** : 17 Août 2026
- **Composants ciblés** : `shaders/bloom_spd_downsample.comp`, `src/vk_engine_bloom.cpp`, `RenderGraph`, `VulkanRHI`.
- **Statut** : 🔴 **Post-Mortem & Reverté (Non Retenu en Production - Priorité Stabilité ISO)**

______________________________________________________________________

## 0. Post-Mortem Métrologique & Analyse des Régressions Visuelles

### 0.1 Bilan de l'Expérimentation Prototype

Bien que le prototype Single-Pass Compute avec `subgroupQuadSwap` ait atteint un temps d'exécution théorique ultra-faible ($60\\ \\mu\\text{s}$ sur la descente), il a provoqué des **artefacts visuels majeurs (damier multicolore / bruit de quantification)** lors de l'exécution interactive :

```text
 ┌──────────────────────────────────────────────────────────────────────────────────┐
 │ POST-MORTEM TECHNIQUE : POURQUOI LE PROTO SINGLE-PASS A ÉCHOUÉ                   │
 ├──────────────────────────────────────────────────────────────────────────────────┤
 │ 1. Inadéquation Subgroup Quad 1D/2D :                                            │
 │    Sur architecture Intel Xe, les 4 lanes d'un Quad SIMD ne correspondent        │
 │    pas à une tuile géométrique carrée 2x2 dans un workgroup 2D (16x16).           │
 │ 2. Lectures LDS Non-Initialisées :                                               │
 │    Les threads inactifs lors des étapes Mip 2..4 ont lu des cellules mémoires    │
 │    non écrites dans s_LDS, introduisant des valeurs garbage et des NaNs.        │
 │ 3. Amplification en Upsampling :                                                 │
 │    La chaîne d'upsampling additif a amplifié ce bruit en blocs géants sur l'écran.│
 └──────────────────────────────────────────────────────────────────────────────────┘
```

### 0.2 Décision d'Architecture Définitive

Conformément à la politique qualité et stabilité du projet :

1. **Revert Intégral du Prototype SPD** : Suppression de `bloom_spd_downsample.comp` et nettoyage du code C++.
1. **Consolidation du Levier 5 (Multi-Pass B10G11R11)** : Solution retenue en production. Temps GPU maîtrisé ($0.337\\text{ ms}$, $>425\\text{ FPS}$), **100% de parité visuelle ISO stricte (RMSE = 0.0)** et 0 risque de corruption mémoire.

______________________________________________________________________

## 1. Objectifs, KPI & Scorecard Décisionnel

### 1.1 Matrice d'Évaluation ROI (Comparatif avec Levier 5)

| Critère | **Levier 5 : B10G11R11 + VMA Aliasing** | **Levier AMD SPD : Single-Pass Downsample** | Avantage |
| :--- | :--- | :--- | :--- |
| **Portée des Gains** | **100% de la chaîne** (5 Downsamples + 4 Upsamples + PostProcess) | **Uniquement les 5 Downsamples** (Upsample inchangé) | 🏆 **Levier 5** |
| **Gains Bande Passante** | **-50% DRAM direct** (8 $\\to$ 4 octets/pixel partout) | -65% DRAM uniquement sur le Downsample | 🏆 **Levier 5** |
| **Gain VRAM Total** | **-75% VRAM** ($5.5\\text{ MB} \\to 1.4\\text{ MB}$) | 0% (même allocation VRAM) | 🏆 **Levier 5** |
| **Temps de Dév.** | **Très court** (1 à 2 itérations, modifications ciblées) | Modéré/Long (Shader complexe, atomiques, LDS) | 🏆 **Levier 5** |
| **Risques / Bugs** | **Quasi-Nul** (Format standard Vulkan 1.2+, fallback propre) | Modéré (Tailles wave 16/32/64, deadlock GPU) | 🏆 **Levier 5** |
| **Impact GPU Intégré (UMA)**| **Maximum** (le bus mémoire DDR5/LPDDR4x est le bottleneck n°1) | Moyen (réduit l'ALU et les stalls de barrières) | 🏆 **Levier 5** |
| **Score ROI Global** | 🟢 **9.5 / 10** | 🟡 **7.0 / 10** (Phase 2 idéale après Levier 5) | 🏆 **Levier 5** |

### 1.2 Métriques Cibles

| Métrique | Implémentation Multi-Pass Actuelle | Cible Single-Pass SPD + Subgroups | Gain Espéré |
| :--- | :--- | :--- | :--- |
| **Dispatches Compute Downsample** | $5\\text{ dispatches}$ séquentiels | **$1\\text{ seul dispatch}$ unifié** | 🚀 **-80% d'appels `vkCmdDispatch`** |
| **Barrières Pipeline Vulkan** | 5 barrières `CmdPipelineBarrier` | **0 barrière hôte** (LDS + atomiques internes) | ⚡ **Zéro stall de synchronisation CPU/GPU** |
| **Allers-Retours Mémoire DRAM** | Écriture + relecture de chaque Mip | **Tampons intermédiaires en LDS / L2** | 📉 **-65% trafic mémoire sur la descente** |
| **Durée Compute Downsample (1080p)**| $~0.55\\text{ ms}$ | **$~0.18 - 0.25\\text{ ms}$** | 🚀 **Temps divisé par 2.5x** |

______________________________________________________________________

## 2. Audit Matériel & Capacités Vulkan (Validations Local GPU & Justifications)

Avant toute implémentation du Single-Pass Downsampler, un audit précis des extensions et des caractéristiques de bas niveau du GPU local (Intel Iris Xe RPL-U, Vulkan 1.4) a été mené :

### 2.1 Caractéristiques Détectées et Justifications Techniques

| Propriété / Extension Vulkan | Valeur Mesurée | Rôle & Justification Technique pour le Single-Pass SPD |
| :--- | :--- | :--- |
| **`subgroupSize`** | **`32`** | Détermine la taille matérielle de la wave SIMD. Un workgroup de 256 threads ($16\\times16$) est découpé exactement en 8 sous-groupes de 32 threads ($256/32=8$). Permet d'éviter toute hypothèse erronée sur la taille de wave (64 sur AMD GCN vs 32 sur Nvidia/Intel). |
| **`SUBGROUP_FEATURE_QUAD_BIT`** (`GL_KHR_shader_subgroup_quad`) | **`Supported`** | **Élément clé** : permet aux 4 threads d'un Quad $2\\times2$ d'échanger leurs scalaires via `subgroupQuadSwapHorizontal` et `subgroupQuadSwapVertical` directement dans les registres ALUs SIMD (sans passer par la mémoire partagée LDS). **Divise par 4 les écritures LDS** lors de la réduction Mip 0 $\\to$ Mip 1. |
| **`SUBGROUP_FEATURE_SHUFFLE_BIT`** (`GL_KHR_shader_subgroup_shuffle`) | **`Supported`** | Permet l'échange arbitraire d'échantillons entre les 32 invocations d'un sous-groupe avec une latence d'un seul cycle horloge ALU. |
| **`SUBGROUP_FEATURE_BASIC_BIT`** (`GL_KHR_shader_subgroup_basic`) | **`Supported`** | Fournit les identifiants d'invocations `gl_SubgroupInvocationID` et `gl_NumSubgroups` nécessaires au partitionnement logique des tuiles. |
| **`computeFullSubgroups`** | **`true`** | Garantit que tous les sous-groupes d'un workgroup de 256 threads contiennent exactement 32 invocations actives (aucune wave partielle/fragmentée), éliminant les risques de blocage sur les opérations de Quad. |
| **`VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT`** (`B10G11R11_UFLOAT`) | **`Supported`** | Permet au shader d'écrire directement dans les 5 mips compacts 32 bits de sortie (`u_down_mips[0..4]`) depuis un seul descriptor set. |
| **Atomiques Globaux sur Tampons SSBO** | **`Supported`** | Permet à l'ensemble des 510 workgroups ($30\\times17$ tuiles en 1080p) d'incrémenter de manière thread-safe un compteur 32 bits (`atomicAdd(counter, 1)`) pour coordonner la fin de la passe sans barrière pipeline CPU/GPU hôte. |

______________________________________________________________________

## 3. Architecture & Fonctionnement Matériel (AMD SPD)

```text
                                  1 SEUL DISPATCH COMPUTE
        ┌────────────────────────────────────────────────────────────────────────┐
        │ Workgroup (256 threads) traite tuile 64x64                             │
        │                                                                        │
        │ 1. Échantillonnage Texture Source 1080p                                │
        │    └── Filtrage Jimenez 13-Tap + Soft-Knee + Karis                     │
        │    └── Écriture directe Mip 0 (540p)                                   │
        │                                                                        │
        │ 2. Réduction Subgroup SIMD (Wave Shuffle / Quad Swap)                  │
        │    └── Réduction 2x2 intra-wave sans mémoire partagée                  │
        │    └── Écriture Mip 1 (270p) en LDS (Local Data Share)                 │
        │                                                                        │
        │ 3. Réduction Intra-LDS (Barrière mémoire locale)                       │
        │    └── Écriture Mip 2 (135p), Mip 3 (68p), Mip 4 (34p) en LDS          │
        │                                                                        │
        │ 4. Coordination Inter-Workgroups via Compteur Atomique Global          │
        │    └── atomicAdd(counter, 1) sur SSBO global                           │
        │    └── Le DERNIER Workgroup calcule le sommet de la pyramide (Mips 5+) │
        └────────────────────────────────────────────────────────────────────────┘
```

______________________________________________________________________

## 3. Étapes d'Intégration Détaillées

### Étape 1 : Conception du Compute Shader SPD (`bloom_spd.comp`)

- Définir le layout de mémoire partagée LDS :
  `shared vec3 s_LDS[16][16];`
- Utiliser les intrinsèques de sous-groupes GLSL :
  `#extension GL_KHR_shader_subgroup_basic : require`
  `#extension GL_KHR_shader_subgroup_quad : require`
  `#extension GL_KHR_shader_subgroup_shuffle : require`

### Étape 2 : Câblage du Tampon Compteur Atomique SSBO

- Allouer un buffer Vulkan de 4 octets (`VK_BUFFER_USAGE_STORAGE_BUFFER_BIT`) réinitialisé à 0 par trame (ou via push constant offset).

### Étape 3 : Intégration au RenderGraph

- Remplacer les 5 nœuds de passes de downsampling par **1 seul nœud `BloomDownsampleSinglePass`** qui prend en entrée `HDRColorTarget` et produit l'ensemble des 5 `downMips`.

______________________________________________________________________

## 4. Risques Encourus & Mitigations

### 4.1 Risque 1 : Taille Variable des Subgroups selon le GPU (16 / 32 / 64)

- *Matériel* : Intel = 16 ou 32, Nvidia = 32 (Warp), AMD = 64 (Wavefront) ou 32 (RDNA).
- *Mitigation* : Utiliser `subgroupQuadSwap` qui est garanti de taille 4 (Quad) sur tout matériel supportant `VK_KHR_shader_subgroup_quad`, avec fallback LDS si `subgroupSize < 16`.

### 4.2 Risque 2 : Deadlock GPU sur Mauvaise Barrière de Mémoire Locale

- *Impact* : Blocage du GPU si `barrier()` / `memoryBarrierShared()` est appelé dans une branche divergente.
- *Mitigation* : Placer toutes les barrières en flux convergent strict et tester avec Vulkan Validation Layers + spirv-val.

______________________________________________________________________

## 5. Protocole de Validation, Non-Régression & Métrologie

### 5.1 Validation Fonctionnelle

- **Tests d'Intégration & Golden Images** : Exécution de `just test-all` comparant le résultat SPD au Golden Standard avec un seuil de tolérance strict (RMSE $< 2.0$).

### 5.2 Profilage & Métrologie

1. **GPU Timeline (Tracy)** :
   - Zone `Bloom Downsample` mesurée individuellement.
   - Vérification de l'absence de stalls d'ordonnancement GPU.
1. **LDS & Registres (Shader Statistics)** :
   - Analyser l'occupation GPU avec `glslc -S` / AMD RGA (Radeon GPU Analyzer) pour s'assurer que l'usage LDS n'entrave pas le nombre de waves simultanées par Compute Unit.
