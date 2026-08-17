# Profiling Tracy

Cette premiere integration Tracy reste volontairement minimale:

- dependance Tracy recuperee via `FetchContent`
- version epinglee sur la derniere release disponible au moment de l'integration: `v0.13.1`
- build applicatif dedie avec `ENABLE_TRACY=ON`
- build du profiler upstream en mode Linux `LEGACY=ON` pour forcer le backend X11
- wrapper client minimal aligne sur la strategie auto-lifecycle du legacy `suckless-ogl`

L'objectif est de disposer d'une base compilee et reproductible avant d'ajouter les zones CPU/GPU dans le code Vulkan.

## Recettes disponibles

Construire l'application avec le client Tracy active:

```bash
just build-tracy
```

Construire l'outil de capture CLI Tracy (sans GUI):

```bash
just build-tracy-capture
```

Lancer l'application instrumentee:

```bash
just tracy-profiler
just run-tracy
```

Construire le profiler Tracy upstream:

```bash
just build-tracy-profiler
```

Lancer le profiler Tracy local:

```bash
just tracy-profiler
```

Lancer un scenario d'integration automatise inspire du legacy `suckless-ogl` (Xvfb + xdotool):

```bash
just test-integration-tracy
```

Ce workflow:

- demarre un display virtuel X11 via `Xvfb`
- lance `tracy-capture` (CLI) pour enregistrer une trace `.tracy` sans interaction GUI
- demarre l'application Tracy
- pilote la fenetre avec `xdotool` (PageUp/PageDown, Shift+PageUp/PageDown, F11, Escape)
- valide que le fichier de trace est bien genere

Arguments utiles:

```bash
just test-integration-tracy capture_seconds=18 trace_file=build/tracy/hdr_ibl.tracy
```

Valider automatiquement les invariants de trace Tracy (zones GPU matérielles et passes RenderGraph) :

```bash
just verify-tracy-trace
just verify-tracy-trace trace_file=build/tracy/hdr_ibl.tracy
```

Le script `scripts/verify_tracy_trace.py` vérifie :

- l'émission active d'événements GPU sur `Vulkan Graphics Queue`
- la présence des passes de rendu par frame (`Forward Pass`, `GPU Geometry`, `GPU Skybox`, `PostProcess Pass`)
- la validité physique des timestamps matériels (`GPU Time > 0`)
- à la demande, la complétion des passes compute IBL (`--require-compute-passes`)

## Details d'implementation

Le projet active Tracy via l'option CMake locale suivante:

```bash
-DENABLE_TRACY=ON
```

Quand cette option est active:

- le depot `https://github.com/wolfpld/tracy.git` est recupere par CMake
- la cible `Tracy::TracyClient` est liee aux binaires du projet
- le profiler upstream est configure depuis la source fetchée en `build/tracy/_deps/tracy-src/`

## Comportement de connexion

Le client Vulkan suit maintenant le meme principe que `suckless-ogl`:

- Tracy gere son initialisation automatiquement des qu'il est lie au binaire
- le wrapper local enregistre uniquement le nom du programme et le thread principal
- un `FrameMark` est emis a chaque frame pour garder la capture exploitable
- la fermeture ne fait pas d'appel manuel bloquant: Tracy termine seul lors de la sortie du processus
- l'application ne depend pas d'un attachement synchrone du profiler pour continuer son execution

Le profiler est compile avec:

```bash
cmake -B build/tracy-profiler \
  -S build/tracy/_deps/tracy-src/profiler \
  -DCMAKE_BUILD_TYPE=Release \
  -DLEGACY=ON
```

Le flag `LEGACY=ON` force le backend Linux historique base sur X11/GLFW. C'est le choix le plus simple pour demarrer ici, et c'est celui qui se rapproche du workflow du projet legacy `suckless-ogl`.

## Prerequis Linux

Pour le client Tracy dans l'application:

- un compilateur C++ compatible CMake/C++17 cote projet
- acces reseau au premier configure pour telecharger Tracy

Pour le profiler Tracy en mode X11 legacy:

- bibliotheques de dev X11 installees
- OpenGL/GLX disponible pour l'interface du profiler

Si la configuration du profiler echoue sur Linux, le point de blocage le plus probable est l'absence des paquets de developpement X11 necessaires au backend legacy.

## Suite logique

Une fois cette base validee, l'etape suivante consiste a ajouter une instrumentation tres ciblee:

- frame mark principal
- quelques zones CPU autour de la boucle frame
- contexte GPU Tracy Vulkan pour les passes majeures

## Premiere instrumentation CPU/GPU

La premiere tranche fonctionnelle d'instrumentation ajoutee dans le moteur couvre volontairement peu de surface:

- une zone CPU sur `vk_init_vulkan_engine`
- une zone CPU sur `vk_cleanup_vulkan_engine`
- une zone CPU principale sur `vk_draw_frame_internal`
- des sous-zones CPU sur les phases `Update`, `Acquire`, `Record`, `Submit` et `Present`
- un contexte Tracy Vulkan lie a la queue graphique principale et au command buffer principal
- des zones GPU minimales `GPU Frame`, `GPU Skybox` et `GPU Spheres`

Une extension ciblee a ensuite ete ajoutee pour la phase IBL HDR asynchrone:

- nommage explicite du thread worker `HDR I/O Thread`
- zone CPU sur la mise en file de demande de switch HDR (`request_environment_texture_async`)
- zone CPU par iteration worker (`hdr_io_thread_iteration`)
- sous-zone CPU sur la phase decode STBI (`hdr_io_thread_decode_stbi`)
- zone CPU sur l'application render-thread de la requete prete (`vk_process_ready_environment_texture`)

Une tranche GPU complementaire couvre desormais les dispatch compute IBL:

- zone GPU `GPU IBL Luminance` (reduction luminance pass 1 + pass 2)
- zone GPU `GPU IBL BRDF LUT`

## Modules Dynamiques (.so) & Persistance des Métadonnées (`RTLD_NODELETE`)

Le moteur charge dynamiquement son backend de rendu (`libvulkan_rhi.so`) via `dlopen`.

Lors du profiling avec Tracy :

- Chaque zone de profiling (`ZoneScopedNC`, `ZoneNamedNC`) génère une structure statique `tracy::SourceLocationData` résidant dans la section `.rodata` de la bibliothèque partagée `libvulkan_rhi.so`.
- Si `dlclose()` est appelé lors de l'arrêt de l'application (`main()`), la mémoire contenant les chaînes de caractères (nom de la zone, fonction, fichier) est désallouée de l'espace d'adressage du processus avant que le client d'arrière-plan de Tracy ne transmette ces informations au serveur/fichier de trace.
- En conséquence, les zones exécutées en fin de cycle de vie (telles que `Engine: Shutdown Vulkan`) apparaissaient sous le libellé de secours `???` dans Tracy Profiler.

### Solution Appliquée

Dans \[`src/module_loader.cpp`\](../src/module_loader.cpp), lors de la compilation avec `TRACY_ENABLE`, le drapeau POSIX `RTLD_NODELETE` est ajouté au `dlopen` :

```cpp
#ifdef TRACY_ENABLE
    handle = dlopen(full_path.c_str(), RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
#else
    handle = dlopen(full_path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
```

Ce drapeau indique au chargeur dynamique du système d'exploitation (`ld.so`) de ne pas démapper la table de symboles et les segments de lecture seule du module lors d'un `dlclose()`, garantissant ainsi la validité et la lisibilité permanente de 100% des métadonnées de profiling jusqu'à la fermeture complète du processus.
