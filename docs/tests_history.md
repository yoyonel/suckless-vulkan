# Historique des Résultats de Tests Locaux

Ce document trace l'état des recettes de tests locales (`just test-*`) au fil du temps. Chaque entrée est horodatée.

## 2026-08-10 00:35:00+02:00 (Branche: feature/rhi-refactor)

| Recette | Résultat | Remarques |
|---|---|---|
| `just test-logic` | **PASSED** | Tests unitaires (MockRHI + CoreEngine) valides. |
| `just test-integration` | **PASSED** | Rendu Vulkan réel headless validé. (Les buffers et frames ne fuient pas). |
| `just test-asan` | **PASSED** | Plus aucune fuite de mémoire détectée (fini les 18040 octets d'IBL views et les 24 octets de `m_mainCmdList`). |
| `just test-validation-layers` | **PASSED** | Zéro erreur ou warning émis par les Vulkan Validation Layers. |
| `just test-coverage` | **PASSED** | La stack LLVM-cov compile et s'exécute correctement sans crash. |
| `just test-integration-tracy` | **PASSED** | Compilation et exécution du rendu avec le profiler Tracy activé sans heurts. |
| `just test-oom` | **PASSED** | L'app consomme moins de RAM virtuelle qu'avant ! Limite abaissée à 100MB dans le script. |
