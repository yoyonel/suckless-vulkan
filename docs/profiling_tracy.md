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

Le but de cette tranche n'est pas encore l'analyse fine des performances, mais la validation fonctionnelle de l'integration du profiler Tracy cote CPU et cote Vulkan.

Cette page sera enrichie quand les premieres zones Tracy seront posees dans le moteur.
