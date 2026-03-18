# Système de Validation IBL (Legacy OGL vs Vulkan)

Cette documentation explique la méthodologie et les outils utilisés pour garantir que le portage de la pipeline IBL (Image Based Lighting) de OpenGL vers Vulkan est mathématiquement identique à la "Vérité Terrain" du moteur legacy.

## 1. Philosophie de Validation

Le rendu PBR (Physically Based Rendering) repose sur des bakes complexes (BRDF, Irradiance, Specular Prefilter). Une simple vérification visuelle ne suffit pas à détecter des erreurs de précision, des décalages de demi-pixels ou des constantes mathématiques erronées dans les shaders.

Nous utilisons une **Analyse Comparative Automatisée** basée sur des dumps HDR.

## 2. Le Workflow de Comparaison

Le workflow est orchestré par la commande `just verify-ibl` :

1. **Génération OGL (Référence) :** Lance `suckless-ogl`, effectue les bakes IBL et dump les textures GPU au format `.hdr` dans `/tmp/ibl_tests/ogl`.
1. **Génération Vulkan (Cible) :** Lance `suckless-vulkan` avec le flag `SVK_IBL_DUMP=1`, effectue les mêmes bakes et dump les résultats dans `/tmp/ibl_tests/vk`.
1. **Analyse Mathématique :** Exécute le script `verify_ibl.py` via `uv run` pour comparer les fichiers deux à deux.

## 3. Métriques de Comparaison

Le script de vérification utilise deux métriques complémentaires pour valider chaque map :

### MSE (Mean Squared Error) — Précision Brute

Le MSE calcule la moyenne du carré des différences de valeur pour chaque canal (R, G, B) de chaque pixel.

- **Pourquoi ?** Pour détecter les dérives de calcul shaders.
- **Seuil de succès :** `< 0.001`. Une valeur supérieure indique souvent une formule mathématique différente ou une constante (ex: PI) mal définie.

### SSIM (Structural Similarity Index) — Fidélité Structurelle

Le SSIM est une métrique avancée qui simule la perception humaine en comparant les contrastes, les structures de texture et la luminance locale.

- **Pourquoi ?** Pour s'assurer que les mips de la map de préfiltrage GGX conservent exactement la même distribution de lumière.
- **Seuil de succès :** `> 0.99`. Un score proche de 1.0 garantit une identité structurelle quasi-parfaite.

## 4. Tooling Python avec `uv`

Le script `scripts/verify_ibl.py` utilise les **Inline Script Metadata** (PEP 723).

```python
# /// script
# dependencies = [ "numpy", "imageio", "scikit-image" ]
# ///
```

Grâce à `uv run`, aucune installation manuelle de dépendances n'est requise. `uv` crée un environnement virtuel éphémère, installe `numpy` (calcul), `imageio` (lecture HDR) et `scikit-image` (SSIM) automatiquement lors de l'exécution.

## 5. Guide de Résolution des Écarts

Si une map affiche un statut **FAIL** :

| Symptôme | Cause probable |
| :--- | :--- |
| **MSE élevé, SSIM élevé** | Différence de précision (ex: utilisation de `lowp` vs `highp`) ou constante PI légèrement différente. |
| **MSE élevé, SSIM bas** | Inversion d'axe (Y-up vs Y-down), erreur de mapping UV équirectangulaire, ou mauvaise formule de NDF GGX. |
| **MISSING** | Le moteur a crashé avant de finir le bake ou l'export HDR a échoué (vérifier les permissions de `/tmp/ibl_tests`). |

## 6. Utilisation

Pour lancer la suite de validation complète :

```bash
just verify-ibl
```

Les résultats sont affichés sous forme de tableau dans la console et les images comparées restent disponibles dans `/tmp/ibl_tests/` pour une inspection manuelle dans un viewer HDR (type GIMP ou Tev).
