# SKILLS.md - Manuel d'Instruction pour Agent Autonome (suckless-vulkan)

Ce document liste les compétences (Skills) critiques que l'agent autonome doit maîtriser pour optimiser, profiler et tester ce moteur Vulkan orienté Data-Oriented Design (SoA).

---

## 1. Gestion de la Mémoire et des Caches (CPU/RAM)

### Skill: Détection des Fuites et Corruption Mémoire (ASan / UBsan)
- **Commande :** `just test-asan`
- **Description :** Exécute les tests avec AddressSanitizer et UndefinedBehaviorSanitizer activés, utilisant les suppressions définies dans `.asan_ignorefile`.
- **Critères de succès :** La commande doit retourner un code `0`. L'absence d'erreurs ASan (pas de "heap-use-after-free", "heap-buffer-overflow", "memory leak") dans la sortie standard (`stdout`/`stderr`) confirme le succès.

### Skill: Analyse Mémoire Profonde avec Valgrind
- **Commande :** `valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --error-exitcode=1 ./build/debug/vulkan_app --no-vsync`
- **Description :** Analyse approfondie de la mémoire pour traquer les fuites complexes et les accès mémoire non initialisés.
- **Critères de succès :** Le résumé Valgrind doit afficher `ERROR SUMMARY: 0 errors from 0 contexts`. Aucune fuite définitive (`definitely lost: 0 bytes`) ne doit être rapportée.

### Skill: Profiling des Allocations et Défauts de Cache (HeapTrack & Perf)
- **Commande :** `just benchmark-heaptrack`
- **Description :** Lance un pipeline utilisant `heaptrack` et `perf` (via le runner interactif) pour mesurer les allocations dynamiques et analyser l'efficacité d'accès aux structures de données (cache misses).
- **Critères de succès :** Le fichier `heaptrack_summary.txt` est généré dans `heaptrack_results/`. L'agent doit parser ce fichier pour s'assurer d'une réduction des `peak heap memory consumption` et d'un faible nombre d'allocations temporaires. Pour les défauts de cache, `perf stat` doit afficher un taux minimal de `L1-dcache-load-misses` dans les logs générés, validant les optimisations SoA.

---

## 2. Optimisation et Profiling GPU (Vulkan)

### Skill: Capture et Analyse RenderDoc
- **Commande :** `renderdoccmd capture -c ./build/release/vulkan_app --no-vsync`
- **Description :** Instrumente et capture les frames Vulkan pour l'analyse des draw calls, du pipeline d'état et des ressources GPU.
- **Critères de succès :** Un fichier `.rdc` est généré avec succès. L'agent peut extraire et analyser le log de capture pour s'assurer de l'absence d'erreurs de validation Vulkan (vérifiables en amont via `just test-validation-layers`) et de la soumission correcte des commandes de rendu.

### Skill: Télémétrie des Temps de Rendu avec MangoHud
- **Commande :** `MANGOHUD=1 MANGOHUD_CONFIG=cpu_temp,gpu_temp,ram,vram,fps,frametime ./build/release/vulkan_app --no-vsync`
- **Description :** Extrait la télémétrie des temps de rendu et les statistiques système en direct.
- **Critères de succès :** Le HUD doit s'injecter sans faire crasher l'application. L'agent doit monitorer les logs pour s'assurer que les temps de frame (`frametime`) sont stables et qu'aucun stuttering massif n'apparaît, bien que `MangoHud` puisse parfois être bruité (préférer Tracy pour la micro-optimisation).

---

## 3. Benchmarking et Télémétrie Globale

### Skill: Profiling Systémique avec Tracy
- **Commande :** `just benchmark-tracy` suivi de l'analyse avec `python3 scripts/analyze_fps.py`
- **Description :** Benchmark automatisé (Headless) qui capture les traces CPU/GPU avec Tracy pour identifier les goulots d'étranglement entre le CPU et la soumission Vulkan.
- **Règle Stricte d'Analyse (Percentiles) :** L'agent **doit** monitorer les percentiles (97%, AVG, 1%, 0.1%). Toute proposition d'optimisation doit augmenter **toutes ces métriques simultanément**.
- **Critères de succès :** Le script `analyze_fps.py` (ou l'analyse du `tracy_stats.csv`) affiche des métriques FPS améliorées ou stables sur tous les percentiles critiques. Aucune dégradation n'est tolérée sur les `0.1% Low`.

### Skill: Analyse des Accès Mémoire avec VTune
- **Commande :** `just benchmark-vtune`
- **Description :** Utilise Intel VTune Profiler pour une analyse hardware (`memory-access`) des goulots d'étranglement de la bande passante DRAM.
- **Critères de succès :** Le dossier `vtune_results/` est généré. L'agent s'appuie sur ces résultats pour justifier les réorganisations de structures de données (SoA), avec pour objectif la réduction du "Memory Bound" global.

---

## 4. Tests Automatisés et Non-Régression

### Skill: Exécution Headless Automatisée
- **Commande :** `xvfb-run -a -s "-screen 0 1920x1080x24" ./scripts/interactive_runner.sh ./build/release/vulkan_app`
- **Description :** Lance le moteur Vulkan dans un environnement headless `Xvfb` (sans écran physique) et simule des inputs (clavier/souris) via `xdotool` pour valider l'interactivité et déclencher des chargements asynchrones.
- **Critères de succès :** L'application démarre, reçoit les inputs (comme attesté par la mention "SUCCÈS: Changement HDR asynchrone confirmé par les logs" dans `runner_app.log`), et se ferme proprement (code `0`).

### Skill: Validation Visuelle et Zéro Régression
- **Commande :** `just test-integration` (Génération des Golden Images avec `SVK_UPDATE_REFERENCES=1 just test-integration`)
- **Règle de Zéro Régression Absolue :** Aucune perte de précision de rendu n'est tolérée. La validation est stricte via les images de référence (Golden Images). Toute modification du code doit maintenir la propreté du code et ne dégrader aucune fonctionnalité.
- **Critères de succès :** Le test d'intégration `EngineIntegrationTest` passe avec succès. Le calcul du RMSE (Root Mean Square Error) entre l'image rendue et la Golden Image doit être inférieur au seuil de tolérance défini. La sortie CTest indique `100% tests passed, 0 tests failed`.
