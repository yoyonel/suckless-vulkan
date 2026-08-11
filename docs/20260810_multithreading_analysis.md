# Analyse d'Optimisation Multithreading & Concurrence (10 Août 2026)

## 1. Synchronisations GPU Fatales (`vkDeviceWaitIdle` / `vkQueueWaitIdle`)

**Problème** : `vkDeviceWaitIdle` bloque le CPU entier pendant l'upload HDR. `vkQueueWaitIdle` gèle la "hot loop" pendant l'exécution des compute shaders de l'IBL bake (stalls de 100ms à 1000ms+).
**Correction (Vulkan Asynchrone)** :

- Remplacer les uploads synchrones par une `Transfer Queue` dédiée (ou Command Buffer asynchrone).
- Submit le `CommandBuffer` de l'IBL Bake avec une `VkFence`.
- Polling via `vkGetFenceStatus` au début de la frame (non-bloquant).
  **Degré de certitude (Gain FPS 0.1% Low)** : **99%**.

## 2. Système de Log Synchrone & Mutex Global

**Problème** : `log_get_level()` et `log_message_v` verrouillent `g_log_mutex`. L'I/O console (stdout) est bloquant et impacte le thread appelant.
**Correction (Lock-free + Threading)** :

- Remplacer `g_log_level` par `std::atomic<LogLevel>` (`std::memory_order_relaxed`).
- Déléguer l'écriture à un thread de log dédié via un Ring Buffer SPSC (Single-Producer Single-Consumer).
  **Degré de certitude (Gain FPS AVG & 1% Low)** : **95%**.

## 3. HDR Loader : `try_to_lock` vs SPSC Lock-Free

**Problème** : `try_to_lock` crée de la contention mémoire (bus L1/L2 Cache Coherence Protocol). `std::queue` alloue dynamiquement sur le tas.
**Correction (SPSC Queue sans allocation)** :

- Remplacer `std::queue` et `std::mutex` par un SPSC Lock-Free Queue (Array statique + indices atomiques `head`/`tail`).
  **Degré de certitude (Gain Stabilité Frametime)** : **85%**.

## 4. False Sharing sur la disposition mémoire (`alignas`)

**Problème** : Variables atomiques lues/écrites par différents threads (Producer/Consumer) sur la même ligne de cache (Cache Line Bouncing).
**Correction (Padding Cache Line)** :

- Isoler les variables du Producer de celles du Consumer avec un padding (`alignas(64)` ou `alignas(128)`).
  **Degré de certitude (Gain micro-optimisation)** : **70%**.

______________________________________________________________________

## 5. Plans d'Implémentation Détaillés (Itérations Timeboxées de 10 min)

### 5.1. Synchronisations GPU Fatales (Vulkan Async)

#### Itération 1 : Async IBL Bake Submission

- **Action** : Remplacer `vkQueueWaitIdle` dans `end_single_time_commands` par un `VkFence` non-bloquant pour l'IBL. Stocker la `VkFence` dans `CoreEngine`.
- **Chances** : 80%. **Risques** : Accès prématuré aux textures IBL non finies si le swap de descriptor set n'est pas synchronisé.

#### Itération 2 : Polling Fence & Swap Descriptor

- **Action** : Ajouter `vkGetFenceStatus` au début de `vk_draw_frame_internal`. Si `VK_SUCCESS`, mettre à jour le `VkDescriptorSet` global et détruire le staging buffer.
- **Chances** : 70%. **Risques** : Fuite mémoire si la fence n'est jamais récupérée ou erreur logique sur le suivi de l'état du load.

#### Évaluation de l'Asynchronisme Vulkan

- **Tests** : Charger un HDR massif (ex: 8K). Vérifier l'absence totale de blocage visuel et l'absence d'artefacts.
- **Benchmark** : MangoHud `0.1% Low`. Le drop massif de framerate au chargement doit être totalement éliminé.
- **Profiling** : Tracy Vulkan. Valider que la frame GPU ne décroche plus pendant l'exécution du compute shader.

### 5.2. Système de Log Lock-Free

#### Itération 1 : Atomic Level Check

- **Action** : Transformer `g_log_level` en `std::atomic<LogLevel>`. Adapter `log_get_level` avec `std::memory_order_relaxed`.
- **Chances** : 95%. **Risques** : Zéro (modification isolée, simple et robuste).

#### Itération 2 : SPSC Queue & Worker Thread

- **Action** : Implémenter Ring Buffer fixe. Lancer un `std::thread` dédié lisant la file en boucle et écrivant sur `stdout`.
- **Chances** : 60%. **Risques** : Débordement de la file (Ring Buffer overflow) bloquant silencieusement les logs, et complexité accrue de l'arrêt du programme (draining the queue).

#### Évaluation du Lock-Free Log

- **Tests** : Spam de `LOG_INFO` via 4 threads simultanés. Vérifier l'ordre et valider qu'aucun log n'est perdu dans la console.
- **Benchmark** : MangoHud `AVG FPS`.
- **Profiling** : `perf` (Linux) ou Intel VTune pour mesurer le temps CPU de la `hot loop` passé dans des Syscalls bloquants (ex: `write()`). Il doit tendre vers 0.

### 5.3. HDR Loader Lock-Free (SPSC)

#### Itération 1 : Structure SPSC Statique

- **Action** : Créer un Ring Buffer statique lock-free (Array + indices atomiques `head`/`tail`). Remplacer les allocations dynamiques de `std::queue`.
- **Chances** : 85%. **Risques** : Cas limites d'indices circulaires (wraparound bugs).

#### Itération 2 : Remplacement Mutex/Try-Lock

- **Action** : Supprimer `hdrLoadMutex` et `hdrLoadCV`. Remplacer l'attente du thread I/O par un Spin-Wait léger (`std::this_thread::sleep_for`).
- **Chances** : 90%. **Risques** : Consommation CPU excessive du thread I/O en attente inactive.

#### Évaluation du HDR Loader SPSC

- **Tests** : Charger/Décharger 10 HDR en boucle rapide (via UI).
- **Benchmark** : Stabilité générale du Frametime, élimination des micro-stutters.
- **Profiling** : ThreadSanitizer (TSan) ou Valgrind (Helgrind) pour prouver rigoureusement l'absence de Data Races sur le Ring Buffer.

### 5.4. Memory False Sharing (Padding)

#### Itération 1 : Isoler les variables atomiques

- **Action** : Séparer physiquement les indices `tail` (I/O thread) et `head` (Render thread) par un padding structuré `alignas(CACHE_LINE_SIZE)` dans `vk_engine.h`.
- **Chances** : 99%. **Risques** : Négligeables (+64/128 octets de consommation mémoire).

#### Évaluation du Padding Cache Line

- **Tests** : Compilation & exécution fonctionnelle basique.
- **Benchmark** : Difficilement mesurable sur les gros métriques, faire un Google Benchmark isolé.
- **Profiling** : `perf c2c` (Cache-2-Cache) sur Linux pour valider la réduction du "Cache Line Bouncing" et des HITM (Hit Modified) inter-cœurs CPU.
