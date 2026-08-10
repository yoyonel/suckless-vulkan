# Iteration Validation Rules

**1. Avant Commit (Checklist Locale)** :
- Lancer `just format` (Code propre)
- Lancer `just lint` (Zéro erreur)
- Lancer `just test-all` (Tests verts)
- Lancer `just pre-commit-run` (Hooks OK)
- Mettre à jour `docs/` et `mkdocs.yml` (si impact)
- Nettoyer Git tree (`git status --short --branch`)

**2. Validation CI / Locale** :
- Vérifier build Docker : `just ci-docker-all`
- Vérifier couverture > 70% : `just coverage-llvm`

**3. Règles Commit & Push (SoC & Autorisation)** :
- Commits découpés par logique (SoC : Séparation des responsabilités)
- Messages au format *Conventional Commits* (`feat:`, `fix:`, `docs:`)
- **INTERDIT** de commit ou push sans validation humaine explicite
- Présenter changements à l'humain avant action

**4. Sécurité Tests App** :
- **INTERDIT** de lancer `just run` ou `build/release/vulkan_app` sans `timeout` (ex: `timeout 5s just run`), sinon blocage infini.

**5. Validation Finale Remote** :
- Une fois poussé, monitorer `gh pr checks`
- Attendre résultat final 100% VERT (SUCCESS)
- **INTERDIT** de déclarer tâche terminée sans preuve 100% GREEN de la CI Github Actions. Aucun skip toléré.

**6. INTERDICTION ABSOLUE DE MERGER** :
- **NE JAMAIS** fusionner (merge) une Pull Request de ta propre initiative.
- Le merge d'une PR est une action destructive exclusive à l'humain.
- Ne lancer la commande `gh pr merge` sous **aucun** prétexte.
