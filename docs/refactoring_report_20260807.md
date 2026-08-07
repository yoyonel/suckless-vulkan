# Refactoring Report - 2026-08-07

## 1. RenderDoc Observability Directive (Mandatory)

- **Fichier**: `src/vk_engine_ibl.cpp`
- **Finding**: Labels RenderDoc manquants sur passes IBL (`IBL_Lum1`, `IBL_Lum2`) dans `vk_ibl_bake`. Labeling commence tard (`IBL_Bake_Pass2`).
- **Problème**: Non-respect `copilot-instructions.md`. Blocs GPU anonymes.
- **Risques**: Débogage RenderDoc illisible. Isolation bugs GPU impossible.
- **Corrections**: Ajouter `vkCmdBeginDebugUtilsLabelEXT` / `vkCmdEndDebugUtilsLabelEXT` autour dispatches `IBL_Lum1` et `IBL_Lum2`.
- **Gains**: Visibilité RenderDoc restaurée. Hiérarchie events claire.
- **Testable**: Exécution `just test-integration` + vérification manuelle UI RenderDoc.

## 2. Mysterious Name

- **Fichier**: `src/vk_engine_ibl.cpp`
- **Finding**: Variables locales obscures: `l1`, `l2`, `ir`, `sp`, `br`, `iI`, `bI`.
- **Problème**: Intention masquée. Contexte métier perdu.
- **Risques**: Maintenance difficile. Bugs lors de futures modifs.
- **Corrections**:
  - `l1` -> `lumPass1Shader`
  - `l2` -> `lumPass2Shader`
  - `ir` -> `irradianceShader`
  - `sp` -> `specularShader`
  - `br` -> `brdfLutShader`
  - `iI` -> `envHdrImageInfo`
  - `bI` -> `lumGroupBufferInfo`
- **Gains**: Code auto-documenté. Charge cognitive nulle.
- **Testable**: Validé par compilateur. Couvert par `just test-integration`.

## 3. Data Clumps

- **Fichier**: `tests/test_main.cpp`

- **Finding**: Paramètres `pixels`, `width`, `height`, `imageSize` dupliqués dans signatures `compare_images`, `readback_frame`, `validate_frame`.

- **Problème**: Données liées non groupées. Manque abstraction "Image".

- **Risques**: Incohérences (ex: buffer mismatch taille). Signatures lourdes.

- **Corrections**:

  ```cpp
  struct FrameBufferData {
      unsigned char* pixels;
      int width;
      int height;
      VkDeviceSize size;
  };
  ```

  Passer `const FrameBufferData&` aux fonctions.

- **Gains**: DRY. API propre. Ajout de champs (ex: `channels`, `format`) sans casser signatures.

- **Testable**: Couvert par suite `just test-all` (tests visuels).

## 4. Duplicated Code

- **Fichier**: `src/vk_engine_ibl.cpp`

- **Finding**: 5 appels identiques `create_compute_pipeline(...) && ...` chaînés.

- **Problème**: Logique création dupliquée 5 fois.

- **Risques**: Oubli/erreur typo sur 1 ligne. Refacto lourde si signature `create_compute_pipeline` change.

- **Corrections**:

  ```cpp
  struct ComputePipelineDesc {
      VkShaderModule shader;
      VkPipelineLayout layout;
      VkPipeline* outPipeline;
      const char* debugName;
  };
  ComputePipelineDesc descs[] = {
      {lumPass1Shader, engine->ibl.lum1PipelineLayout, &engine->ibl.lum1Pipeline, "IBL_Lum1"},
      // ... autres pipelines
  };
  for (const auto& d : descs) {
      if (!create_compute_pipeline(engine->device, d.shader, d.layout, d.outPipeline, d.debugName)) return false;
  }
  ```

- **Gains**: Code compact, évolutif, DRY. Centralisation gestion erreur.

- **Testable**: Validé par compilateur. Assert runtime via `just test-integration` (engine crash si init fail).
