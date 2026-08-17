# Analyse des Traces RenderDoc & Architecture Render Graph (14 Août 2026)

## 1. Contexte & Objectifs

L'intégration du **Render Graph** dans `suckless-vulkan` décompose le pipeline de rendu en plusieurs passes autonomes ordonnancées dynamiquement (`Render_ForwardPass`, `Render_PostProcess`).

Ce document détaille :

1. La structure de l'arbre d'exécution GPU visible dans **RenderDoc**.
1. Le fonctionnement du **Memory Poisoning** et le décodage des **mires de diagnostic internes de RenderDoc** (`UNDEFINED IMG`, `LOAD DONTCARE`).
1. L'impact matériel sur architectures **IMR** (Desktop) vs **TBR/TBDR** (Mobile/Apple Silicon).
1. La méthode d'inspection frame-par-frame des attachements et des transitions de synchronisation (`vkCmdPipelineBarrier`).
1. Les références techniques officielles (Documentation RenderDoc, Spécification Vulkan, GitHub Issues).

______________________________________________________________________

## 2. Décomposition de la Frame sous RenderDoc

Dans l'**Event Browser** de RenderDoc, une frame typique instrumentée avec le RenderGraph se structure comme suit :

```text
▼ Frame #XXXX
  │
  ├── vkCmdPipelineBarrier({ Main_ColorAttachment }) [Transition vers COLOR_ATTACHMENT]
  │
  ├── ▼ Render_ForwardPass
  │     ├── vkCmdBeginRenderPass(C=Clear, D=Clear)  [Main_ColorAttachment, Main_DepthBuffer]
  │     ├── vkCmdBindPipeline(Skybox_Graphics_Pipeline)
  │     ├── vkCmdDraw(3, 1)                         [Skybox IBL]
  │     ├── vkCmdBindPipeline(Billboard_Graphics_Pipeline)
  │     ├── vkCmdDraw(6, 100)                       [Géométrie instanciée]
  │     └── vkCmdEndRenderPass(C=Store, D=Don't Care)
  │
  ├── vkCmdPipelineBarrier({ Main_ColorAttachment, Swapchain Image }) [Synchro inter-passes]
  │     └── Transition ColorAttachment -> SHADER_READ_ONLY_OPTIMAL
  │     └── Transition Swapchain Image -> COLOR_ATTACHMENT_OPTIMAL
  │
  ├── ▼ Render_PostProcess
  │     ├── vkCmdBeginRenderPass(C=Don't Care, D=Don't Care) [Swapchain_Framebuffer]
  │     ├── vkCmdBindPipeline(PostProcess_Pipeline)
  │     ├── vkCmdBindDescriptorSets(PostProcess_Descriptor_Set) [Sampler ColorAttachment]
  │     ├── vkCmdDraw(3, 1)                         [Fullscreen Quad]
  │     └── vkCmdEndRenderPass(C=Store, D=Don't Care)
  │
  └── vkQueuePresentKHR(Swapchain Image)
```

______________________________________________________________________

## 3. Décryptage du Mécanisme de Diagnostic & Mires RenderDoc

### 3.1. Le Principe du "Memory Poisoning" (Undefined Discard Pattern)

Par défaut, sur du matériel réel (notamment les GPU Desktop IMR d'AMD et NVIDIA), une mémoire non initialisée ou configurée en `DONT_CARE` conserve souvent par hasard ses anciennes valeurs ou des zéros résiduels. Cela crée un **faux sentiment de validité** : un shader qui lit un pixel non initialisé peut sembler fonctionner par coïncidence sur une machine de développement, mais produira des artefacts graphiques majeurs (glitches, clignotements) ou des crashs sur un autre GPU.

Pour éliminer ce comportement indéfini (*Undefined Behavior - UB*), **RenderDoc applique une politique stricte d'empoisonnement de mémoire** :

1. À la création des images ou lors d'un `VK_IMAGE_LAYOUT_UNDEFINED`, la mémoire est remplie avec un motif de suppression diagnostique.
1. À l'ouverture d'un `vkCmdBeginRenderPass` avec `VK_ATTACHMENT_LOAD_OP_DONT_CARE`, RenderDoc écrit explicitement le filigrane textuel **`LOAD DONTCARE`**.
1. Si un pixel n'est pas écrasé par un draw call, ce filigrane reste visible à l'écran, pointant immédiatement la zone non écrite.

______________________________________________________________________

### 3.2. Comparatif des Mires Diagnostiques

#### A. Filigrane `UNDEFINED IMG` (Alerte Risque Spécification)

- **Déclencheur Vulkan** : `loadOp = VK_ATTACHMENT_LOAD_OP_LOAD` sur un attachement non initialisé ou issu d'une transition depuis `VK_IMAGE_LAYOUT_UNDEFINED`.
- **Signification** : L'application demande au GPU de *charger et préserver* la mémoire antérieure, mais aucune donnée valide n'a été écrite sur cette frame. RenderDoc remplit la texture avec la mire `UNDEFINED IMG` pour signaler la lecture d'un état indéfini.
- **Correction** : Si le shader suivant réécrit complètement l'image (cas du Post-Process fullscreen), remplacer `LOAD_OP_LOAD` par `LOAD_OP_DONT_CARE`.

#### B. Filigrane `LOAD DONTCARE` (Validation Optimisation)

- **Déclencheur Vulkan** : `loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE`.
- **Signification** : L'application informe le driver qu'elle se désintéresse du contenu précédent. RenderDoc injecte le motif textuel `LOAD DONTCARE` à l'instant **`vkCmdBeginRenderPass`**.
- **Comportement temporel dans RenderDoc** :
  - **À l'EID `vkCmdBeginRenderPass`** : Le Texture Viewer affiche `LOAD DONTCARE` (état initial vérifié).
  - **À l'EID `vkCmdDraw` (Fullscreen Quad)** : Le draw call écrase 100% des pixels de l'écran -> la scène finale rendue apparaît.
  - **À l'EID `vkCmdEndRenderPass` / `vkQueuePresentKHR`** : Aucun filigrane ne subsiste.

______________________________________________________________________

### 3.3. Tableau Récapitulatif & Comportement Matériel

| `loadOp` Vulkan | Texture Viewer à `vkCmdBeginRenderPass` | Texture Viewer après `vkCmdDraw` | Comportement GPU Desktop (IMR) | Comportement GPU Mobile (TBR/TBDR) |
|---|---|---|---|---|
| **`LOAD_OP_CLEAR`** | Couleur de clear uniforme (ex: Noir) | Image composée | Fast-clear matériel via métadonnées | Initialisation de la mémoire tuilée locale |
| **`LOAD_OP_LOAD`** | Image antérieure ou mire `UNDEFINED IMG` | Image avec ajouts successifs | Lecture VRAM complète (coûteux) | Rechargement DRAM -> Tuile locale (très coûteux) |
| **`LOAD_OP_DONT_CARE`** | Filigrane diagnostique `LOAD DONTCARE` | Image écrasée par le draw | Zéro lecture mémoire (rapide) | **Zéro transfert DRAM -> Tuile (optimal)** |

______________________________________________________________________

## 4. Protocole d'Analyse d'une Frame sous RenderDoc

Pour valider une capture du moteur et vérifier l'intégrité de la chaîne graphique :

1. **Vérifier l'ordonnancement du DAG** :
   - Ouvrir l'**Event Browser** et s'assurer que `Render_ForwardPass` s'exécute strictement avant `Render_PostProcess`.
1. **Contrôler la Barrière de Synchronisation Automatique** :
   - Sélectionner l'événement `vkCmdPipelineBarrier` situé entre les deux passes.
   - Dans le panneau **Pipeline State**, vérifier les transitions d'images :
     - `Main_ColorAttachment` : `COLOR_ATTACHMENT_OPTIMAL` -> `SHADER_READ_ONLY_OPTIMAL`.
     - `Swapchain Image` : `UNDEFINED` -> `COLOR_ATTACHMENT_OPTIMAL`.
1. **Inspecter l'Évolution Temporelle des Attachments** :
   - Sélectionner `vkCmdBeginRenderPass` : constater le motif `LOAD DONTCARE` (preuve de l'absence de load VRAM).
   - Sélectionner le `vkCmdDraw(3, 1)` de `Render_PostProcess` ou `vkQueuePresentKHR` : constater la disparition intégrale du motif au profit de l'image tonemappée finale.

______________________________________________________________________

## 5. Références Techniques & Documentation

- [RenderDoc FAQ : Undefined contents & debug patterns](https://renderdoc.org/docs/getting_started/faq.html)
- [RenderDoc GitHub Issue #2045 : Vulkan input attachment LOAD_DONT_CARE behavior](https://github.com/baldurk/renderdoc/issues/2045)
- [RenderDoc GitHub Issue #185 : Undefined to Present layout transitions](https://github.com/baldurk/renderdoc/issues/185)
- [Khronos Vulkan 1.3 Specification : Attachment Load Operation (`VkAttachmentLoadOp`)](https://registry.khronos.org/vulkan/specs/1.3-extensions/html/vkspec.html#renderpass-load-store-ops)
- [Arm Mali GPU Best Practices : Tile-Based Rendering loadOp optimization](https://developer.arm.com/documentation/101897/latest/)
