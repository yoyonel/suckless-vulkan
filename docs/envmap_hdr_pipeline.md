# Envmap HDR Pipeline

Ce document decrit la technique actuelle de rendu/chargement des envmaps HDR dans `suckless-vulkan`, puis la direction cible pour un pipeline asynchrone et optimise CPU/GPU/IO/RAM/VRAM.

## Objectifs

- Rendre une skybox HDR equirectangulaire stable visuellement (infiniment loin).
- Fournir une base robuste en CI, meme sans assets HDR locaux.
- Preparer une migration vers un chargement asynchrone et un traitement progressif.
- Minimiser les stalls CPU et les attentes GPU lors des changements d'envmap.

## Etat Actuel du Rendu Envmap

Le rendu envmap est implemente avec un pipeline fullscreen dedie:

- Vertex shader: `shaders/skybox.vert`
- Fragment shader: `shaders/skybox.frag`
- Initialisation pipeline: `src/vk_engine.cpp`

Principes techniques:

- Draw en fullscreen triangle (pas de vertex buffer scene).
- Reconstruction d'un rayon monde par pixel via `invViewProj`.
- Sampling equirectangulaire dans la map HDR avec `textureLod`.
- `showEnvmap` permet d'activer/desactiver la skybox runtime.
- `envLod` permet d'ajuster le niveau de blur percu via le mip level.

### Camera et stabilite "infinite distance"

Le rendu skybox utilise une vue sans translation:

- `skyboxView = mat4(mat3(view))`
- projection skybox fixe (FOV constant)
- `invViewProj = inverse(skyboxProj * skyboxView)`

Effet attendu:

- La rotation camera affecte l'envmap.
- La translation camera et le zoom gameplay n'affectent pas la position apparente de l'envmap.

## Etat Actuel du Chargement HDR

Le chargement est synchronise au startup de l'engine:

- Recherche du premier `.hdr` dans `assets/textures/hdr`.
- Decode float HDR via `stb_image` (`stbi_loadf`).
- Upload via staging buffer CPU -> image Vulkan (`VK_FORMAT_R32G32B32A32_SFLOAT`).
- Generation de mips par blit si support du format (sinon mip chain reduite a 1).
- Creation de `VkImageView` + `VkSampler` et binding descriptor.

Code principal:

- `find_first_hdr_path()` dans `src/vk_engine.cpp`
- `init_environment_texture()` dans `src/vk_engine.cpp`

## Fallback Actuel (CI et environnements sans assets)

Le pipeline ne doit pas echouer si aucun asset HDR n'est present.

Comportement actuel:

- Si aucun `.hdr` n'est trouve: creation d'une texture fallback 1x1 noire.
- Si un `.hdr` est trouve mais invalide/inchargeable: meme fallback 1x1 noire.
- Le descriptor image/sampler reste valide et l'application continue.

Benefices:

- Les tests integration passent en CI sans dossier `assets/textures/hdr`.
- Le rendu reste deterministe (fond noir) au lieu d'un crash init.

## Limites de l'Implementation Courante

- Chargement synchrone (potentiel freeze startup).
- Pas de prefetch multi-HDR.
- Pas de swap asynchrone atomique des ressources envmap.
- Pas de budget explicite VRAM pour le cache HDR.
- Pas de compression/transcodage intermediaire pour limiter bande passante/empreinte.

## Architecture Cible (Phases Futures)

## Phase A: Catalogue et selection runtime

- Scanner et indexer tous les `.hdr` au startup.
- Exposer `currentHdrIndex` + metadata (nom, resolution, taille).
- Permettre navigation runtime stable (next/prev) sans restart.

## Phase B: Chargement asynchrone disque + decode

- Thread IO dedie pour lecture fichier.
- Decode HDR hors thread render.
- File de requetes avec annulation/coalescing (garder la derniere requete utile).
- Eviter copies inutiles (buffer ownership clair, move semantics).

## Phase C: Upload GPU non bloquant

- Ring de staging buffers reutilisables.
- Command buffers de transfert dedies.
- Synchronisation fine (timeline semaphore ou fences/event selon politique projet).
- Swap atomique des handles image/view/sampler lorsque le nouvel envmap est ready.

## Phase D: Optimisation memoire et bande passante

- Politique de cache (N envmaps max, eviction LRU).
- Mip strategy selon usage (full chain vs chain reduite).
- Budget VRAM configurable.
- Instrumentation: temps decode, temps upload, pic RAM, delta VRAM.

## Phase E: Traitements progressifs IBL

- Integration du prefilter speculaire/irradiance en mode progressif.
- Decouplage rendu principal vs jobs IBL (slicing).
- Telemetrie de convergence et qualite visuelle.

## Criteres de Performance (cibles)

Exemples de KPI a suivre:

- `T_io_read_ms`: lecture fichier HDR
- `T_decode_ms`: decode HDR CPU
- `T_upload_ms`: transfert CPU->GPU
- `T_swap_ms`: activation nouvelle envmap
- `RAM_peak_mb`: pic RAM pendant transition
- `VRAM_delta_mb`: variation VRAM par envmap

Objectif UX cible:

- Pas de hitch perceptible pendant changement envmap.
- Frame time stable pendant chargement asynchrone.

## Notes de Validation

A chaque evolution du pipeline:

- `just format`
- `just lint`
- `just test-all`
- Verification CI GitHub Actions sur PR

## References

- `src/vk_engine.cpp`
- `src/vk_engine.h`
- `src/camera.cpp`
- `shaders/skybox.vert`
- `shaders/skybox.frag`
- `docs/runtime_controls_logging.md`
- `docs/vram_optimization.md`
- `docs/ci_cd.md`
