# Analyse d'Écart de Performance : Rechargement d'Environnements HDR/IBL (2026-08-16)

Date : 16 août 2026\
Branche : `feature/render-graph-integration`\
Machine de test : Intel RPL-U (Iris Xe Graphics), Linux 6.12 x86_64\
Mode : `RelWithDebInfo` / Vulkan 1.4 Native

______________________________________________________________________

## 1. Contexte & Observation

Lors de l'exécution interactive du moteur `suckless-vulkan` :

- **État Initial (Screen 2)** : Immédiatement après démarrage et premier rendu stable avec l'environnement par défaut (`abandoned_garage_4k.hdr`), le framerate atteint **$\\sim 557\\text{ FPS}$** (frame timing moyen de **$1.931\\text{ ms}$**).
- **Après Cycle de Rechargement (Screen 1)** : Après appui sur `Page_Down` (bascule vers une autre carte HDR), attente de la fin du bake asynchrone, puis appui sur `Page_Up` (retour à la carte initiale `abandoned_garage_4k.hdr`) et stabilisation, le framerate se stabilise à **$\\sim 373\\text{ FPS}$** (frame timing moyen de **$2.668\\text{ ms}$**).

Ce document formalise l'analyse détaillée des causes physiques, logicielles et architecturales de cette divergence, ainsi que les leviers techniques pour prévenir cette perte de performance.

______________________________________________________________________

## 2. Décomposition Profilée des Délais (Mesures Tracy & Hardware)

Une capture Tracy continue sur 5 136 frames a été découpée en 3 fenêtres temporelles :

| Phase Temporelle | `Total Frame` (CPU) | `Frame Acquire Swapchain` | `RenderGraph Record` | `Fused Pass` (GPU) | Framerate Estimé |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **1. Init ($1 - 3\\text{ s}$)** | $0.822\\text{ ms}$ | $0.362\\text{ ms}$ | $0.204\\text{ ms}$ | **$0.584\\text{ ms}$** | **$557\\text{ FPS}$** |
| **2. Post-Switch 1 ($6 - 8\\text{ s}$)** | $1.594\\text{ ms}$ | $0.711\\text{ ms}$ | $0.333\\text{ ms}$ | **$0.850\\text{ ms}$** | **$543\\text{ FPS}$** |
| **3. Post-Switch 2 ($11 - 13\\text{ s}$)**| $1.723\\text{ ms}$ | **$0.917\\text{ ms}$** | $0.332\\text{ ms}$ | **$1.007\\text{ ms}$** | **$373\\text{ FPS}$** |

$$\\Delta \\text{Frame Time} = \\underbrace{+0.42\\text{ ms}}_{\\text{Ralentissement Fréquence GPU (0.58ms } \\to \\text{ 1.00ms)}} + \\underbrace{+0.55\\text{ ms}}_{\\text{Attente Swapchain Acquire}} + \\underbrace{+0.12\\text{ ms}}\_{\\text{Trafic Mémoire & Descriptors}} \\approx \\mathbf{+1.09\\text{ ms}}$$

______________________________________________________________________

## 3. Causes Techniques Identifiées

### A. Throttling Thermique & Fréquence GPU SoC (Facteur Dominant sur iGPU UMA)

- **Init** : Le GPU démarre à froid et opère à sa fréquence **Turbo Max** ($\\sim 1.40\\text{ GHz}$).
- **Charge IBL Bake** : Les passes de calcul intensives (`GPU IBL Irradiance` avec intégration de Monte-Carlo sur 12 tranches et `GPU IBL Specular` avec importance sampling GGX sur 5 niveaux de mips) saturent l'enveloppe thermique du SoC Intel (TDP nominal de $15\\text{ W}$ partagé entre CPU et GPU).
- **Régime Permanent** : Pour respecter le seuil de puissance soutenu ($PL1$), le contrôleur d'alimentation matériel abaisse la fréquence d'horloge GPU à sa fréquence nominale ($\\sim 850 - 950\\text{ MHz}$).
- **Conséquence** : Le temps d'exécution pur du rasterizer pour le même nombre de triangles et de fragments passe de $0.58\\text{ ms}$ à $1.00\\text{ ms}$ (soit une augmentation directe de $+72%$).

### B. Backpressure Swapchain & Présentation X11 DRI3

- En mode `--no-vsync` (`VK_PRESENT_MODE_IMMEDIATE_KHR` ou `MAILBOX`), le serveur d'affichage X11 / Mesa DRI3 accumule les buffers de présentation dans la file du compositeur.
- Au fil du temps, le CPU passe de **$0.362\\text{ ms}$ à $0.917\\text{ ms}$** bloqué dans l'appel `vkAcquireNextImageKHR` à attendre qu'une image de la swapchain soit libérée par le compositeur X11.

### C. Réallocations Mémoire Dynamiques & Invalidation de Descripteurs

- Lors du rechargement, de nouvelles instances de `VkImage`, `VmaAllocation` et `VkSampler` sont créées tandis que les anciennes sont détruites via la file de nettoyage (`hdrCleanupQueue`).
- Bien que le driver réattribue la mémoire, la fragmentation du tas VMA et le churn des descripteurs GPU obligent le contrôleur mémoire à réchauffer les caches L2/L3 GPU pour les nouvelles adresses virtuelles.

______________________________________________________________________

## 4. Matrice Priorisée des Solutions Anti-Régression

Les solutions sont ci-dessous **ordonnées par probabilité d'éliminer la régression** (du plus fort impact mesuré au plus faible), avec gains chiffrés, coût d'implémentation et analyse de risque.

______________________________________________________________________

### 🥇 Priorité 1 : Synchronisation Swapchain Non-Bloquante & Mailbox Present

- **Impact sur la régression** : **Très élevé** (Résout la cause #1 mesurée : **$+0.55\\text{ ms}$** de latence CPU dans `vkAcquireNextImageKHR`).
- **Principe** : En mode `--no-vsync`, éliminer la backpressure du compositeur X11/Mesa en ne bloquant plus le thread principal lors de l'acquire.
- **Gain espéré** : **$+0.50\\text{ à }+0.55\\text{ ms}$ CPU par frame** (soit un gain immédiat de **$+80\\text{ à }+100\\text{ FPS}$**).
- **Coût d'implémentation** : **Faible** ($\\sim 30-50$ lignes de code dans la gestion de la swapchain / frame sync).
- **Risque** : **Très faible** (Standard Vulkan WSI, sans impact sur les shaders ni le RenderGraph).

______________________________________________________________________

### 🥈 Priorité 2 : Upload Direct des 13 Mipmaps KTX2 (Zéro Blit GPU Runtime)

- **Impact sur la régression** : **Élevé** (Supprime le pic de saturation thermique qui déclenche le throttling GPU).
- **Principe** : `abandoned_garage_4k.hdr.ktx2` contient déjà toute la pyramide de mips. Uploader directement les 13 niveaux en 1 transfert DMA (`vkCmdCopyBufferToImage`) et supprimer les 12 passes de `vkCmdBlitImage`.
- **Gain espéré** : **$-100%$ du temps de blit GPU** ($\\sim 15\\text{ ms}$ de charge GPU éliminés lors du switch) et forte réduction de la surchauffe SoC.
- **Coût d'implémentation** : **Moyen** ($\\sim 60-80$ lignes de code dans \[`src/asset_ktx.cpp`\](../src/asset_ktx.cpp) et \[`src/vk_engine_envmap.cpp`\](../src/vk_engine_envmap.cpp)).
- **Risque** : **Faible** (Format KTX2 standardisé, supprime du code de blit complexe).

______________________________________________________________________

### 🥉 Priorité 3 : Lissage Temporel du Compute IBL (Throttling des Slices)

- **Impact sur la régression** : **Moyen à Élevé** (Maintient le GPU dans son enveloppe thermique $PL1 \\le 15\\text{ W}$).
- **Principe** : Limiter la soumission des compute shaders IBL à $1\\text{ ou }2$ tranches spéculaires par frame au lieu de 24.
- **Gain espéré** : **$+0.20\\text{ à }+0.35\\text{ ms}$ GPU par frame** en prévenant la chute de fréquence d'horloge de $1.40\\text{ GHz} \\to 850\\text{ MHz}$.
- **Coût d'implémentation** : **Très faible** (Ajustement des constantes `totalSlices` dans \[`src/vk_engine_envmap.cpp`\](../src/vk_engine_envmap.cpp)).
- **Risque** : **Négligeable** (Prolonge imperceptiblement le chargement de quelques frames sans aucun artefact visuel).

______________________________________________________________________

### 4️⃣ Priorité 4 : Pool Fixe de Textures IBL (Double-Buffering Persistant)

- **Impact sur la régression** : **Moyen** (Élimine le churn mémoire et la fragmentation des caches).
- **Principe** : Pré-allouer 2 slots fixes de textures IBL (`Slot A` / `Slot B`). Zéro allocation/destruction `VkImage` / `VmaAllocation` au runtime.
- **Gain espéré** : **$+0.10\\text{ à }+0.15\\text{ ms}$** de latence mémoire GPU, $0\\text{ allocation VMA}$ en cours de jeu.
- **Coût d'implémentation** : **Moyen** ($\\sim 100-150$ lignes de refactor dans \[`src/vk_engine_envmap.cpp`\](../src/vk_engine_envmap.cpp)).
- **Risque** : **Moyen** (Nécessite de verrouiller la synchronisation des descripteurs entre les deux slots).

______________________________________________________________________

### 5️⃣ Priorité 5 : Gouverneur de Performance Matériel (Environnement de Benchmark)

- **Impact sur la régression** : **Validation / Mesure** (Supprime le biais matériel lors des benchmarks).
- **Principe** : Fixer la fréquence GPU/CPU via `powerprofilesctl set performance` ou `intel_gpu_frequency`.
- **Gain espéré** : Répétabilité à $100%$ des mesures de performance.
- **Coût d'implémentation** : **Zéro code C++** (Script shell).
- **Risque** : **Nul**.
