# Tests d'Intégration — Infrastructure & Contraintes GPU/Display

## Vue d'ensemble

Les tests d'intégration lancent l'application Vulkan réelle pour valider le
cycle complet init → rendu → destruction. Ils nécessitent un contexte Vulkan valide
et donc une stratégie adaptée selon l'environnement d'exécution.

## Pattern GPU/Display commun

Tous les scripts (`smoke_test_app.sh`, `run_test_vulkan.sh`, `test_oom.sh`,
`benchmark.sh`, `benchmark_tracy.sh`, `test_integration_tracy.sh`) suivent le même
pattern :

```bash
if [[ "$CI" == "true/1" ]] || [[ -z "$DISPLAY" ]]; then
    # CI ou sans display → Xvfb + llvmpipe
    export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
else
    # Display dispo → GPU réel (iGPU Intel Iris, AMD, etc.)
fi
```

## Contrainte : Intel ICD + Xvfb = incompatible (no DRI3)

### Symptôme

```text
vulkan: No DRI3 support detected - required for presentation
terminate called without an active exception
```

### Cause

Le driver Vulkan Intel (`intel_icd.json`) utilise **DRI3** pour la présentation X11.
**Xvfb ne supporte pas DRI3** — c'est un serveur X virtuel sans accélération hardware.

On pourrait croire qu'en séparant le display X11 (Xvfb) du driver Vulkan (Intel ICD),
les deux seraient indépendants. Ce n'est pas le cas : le driver Intel requiert DRI3
**dans la connexion X11** pour initialiser la surface de présentation.

```text
Xvfb (:99) + VK_ICD_FILENAMES=intel_icd.json → CRASH (no DRI3)
Xvfb (:99) + VK_ICD_FILENAMES=lvp_icd.json  → OK  (llvmpipe, SW only)
DISPLAY=:0  + VK_ICD_FILENAMES=intel_icd.json → OK  (DRI3 via Xorg réel)
```

### Compatibilité Xvfb par driver

| Driver | Compatible Xvfb | Notes |
|--------|----------------|-------|
| `lvp_icd.json` (llvmpipe) | ✅ | SW renderer pur, sans DRI3 |
| `intel_icd.json` (Intel) | ❌ | Requiert DRI3 |
| `radeon_icd.json` (AMD) | ❌ | Requiert DRI3 |
| `nouveau_icd.json` (Nouveau) | ❌ | Requiert DRI3 |

## Stratégie adoptée pour `test-integration-tracy`

### Contrainte spécifique

`test-integration-tracy` capture une trace Tracy de 45 secondes et attend
`"Initialization complete"` (bake IBL terminé). Avec llvmpipe, le bake IBL dépasse
**90 secondes** (12 slices irradiance + specular map en CPU software) — le timeout de
60 secondes était systématiquement dépassé.

### Décision : GPU réel sur DISPLAY existant + `--no-focus`

| Env | Display | GPU | IBL bake | Fenêtre visible |
|-----|---------|-----|----------|----------------|
| Local (`$DISPLAY` set) | `:0` réel | iGPU Intel | **~2s** | Oui, ~50s, sans focus |
| CI (`CI=1`) | Xvfb virtuel | llvmpipe | ~90s+ | Non |
| Sans display | Xvfb virtuel | llvmpipe | ~90s+ | Non |

### Comportement fenêtre en local — acceptable

La fenêtre est visible ~50 secondes pendant la capture Tracy.

**Non intrusive car :**

1. `--no-focus` → `GLFW_FOCUSED=FALSE` → pas de prise de focus clavier/souris
1. `xdotool key --window $wid` → events ciblés sur la fenêtre de test uniquement,
   sans affecter la fenêtre active de l'utilisateur
1. Même comportement que `smoke_test_app.sh` (pattern de référence du projet)

### Suppression de `windowfocus` / `windowactivate`

Ces commandes **volaient le focus WM** — la fenêtre active changeait, le curseur se
déplaçait, toute saisie en cours était interrompue. Elles ont été supprimées :

```bash
# SUPPRIMÉ — volait le focus WM
xdotool windowfocus "$wid"
xdotool windowactivate "$wid"

# Remplacé — renommage inoffensif, keys ciblées
xdotool set_window --name "suckless-vulkan-test" "$wid" 2>/dev/null || true
# Les xdotool key --window $wid dans run_scenario() n'ont pas besoin du focus WM
```

## Bug corrigé : `USE_XVFB:-1` dans `test_integration_tracy.sh`

`USE_XVFB:-1` signifie *"Xvfb activé par défaut"*, ce qui forçait llvmpipe même en
local avec un GPU disponible. Résultat : timeout IBL systématique.

```bash
# AVANT (bug) — Xvfb toujours, llvmpipe, timeout 60s
if [[ "${CI:-}" == "1" ]] || [[ "${USE_XVFB:-1}" == "1" ]]; then

# APRÈS (corrigé) — GPU réel si DISPLAY disponible
if [[ "${CI:-}" == "1" ]] || [[ -z "${DISPLAY:-}" ]]; then
```

## Tableau récapitulatif des scripts

| Script | CI | Local | GPU | Fenêtre |
|--------|----|-------|-----|---------|
| `smoke_test_app.sh` | Xvfb+llvmpipe | DISPLAY direct | réel | 2s, sans focus |
| `run_test_vulkan.sh` | Xvfb+llvmpipe | DISPLAY direct | réel | durée test |
| `test_oom.sh` | Xvfb+llvmpipe | DISPLAY direct | réel | 3s, sans focus |
| `benchmark.sh` | Xvfb+llvmpipe | DISPLAY direct | réel | durée bench |
| `benchmark_tracy.sh` | Xvfb+llvmpipe | DISPLAY direct | réel | durée bench |
| `test_integration_tracy.sh` | Xvfb+llvmpipe | DISPLAY direct | réel | ~50s, sans focus |
