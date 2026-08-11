# Plan d'Implémentation : Phase 5.2 (Lock-Free Logger) & Phase 5.4 (False Sharing)

**Date :** 2026-08-11
**Objectif Global :** Éradiquer les dernières sources de micro-stutter induites par les contentions entre threads (Mutex) et les invalidations de cache L1/L2 (False Sharing) sur l'architecture courante (Intel UMA).

______________________________________________________________________

## Phase 5.2 : Système de Logs Synchrone Lock-Free (MPSC)

### Concept & Motivations

Le système actuel (`app_log.cpp`) utilise un `std::mutex g_log_mutex`. Si le Thread I/O (Background) et le Main Thread (Graphics) écrivent un log au même moment, l'un des deux sera suspendu par l'OS (Context Switch), engendrant un pic de latence (Stutter) sur la frame.
Le but est de basculer sur un paradigme **MPSC** (Multi-Producer, Single-Consumer) : les threads "produisent" des logs dans un Ring Buffer (Lock-Free), et un thread de fond unique "consomme" (écrit sur stdout/disque).

### Blocs d'Itérations (MVP)

#### Bloc 1 : MPSC Ring Buffer (Fondation)

- **Tâche :** Implémenter une structure `LogQueue` sans verrou.
  - Stratégie simple : un tableau circulaire de taille fixe (ex: 1024 slots) avec un index `write_index` atomique (`fetch_add`).
  - Chaque `LOG_INFO` alloue un slot, écrit sa string formattée, et marque le slot comme "Ready".
- **Risque :** Queue pleine. Solution MVP : Si le ring buffer est plein, on *drop* silencieusement le log ou on bascule temporairement en synchrone.

#### Bloc 2 : Thread Consommateur (I/O Logger)

- **Tâche :** Créer un thread `loggerIoThread` lancé au démarrage de l'application.
- **Logique :** Boucle infinie qui lit les slots "Ready" du Ring Buffer, appelle `fmt::print` ou `fwrite`, libère le slot, et dort brièvement (`std::this_thread::yield` ou `wait`).
- **Risque :** Surcharge CPU si le thread spin inutilement. Utilisation de `std::atomic::wait` (C++20) pour endormir le thread proprement quand la queue est vide.

#### Bloc 3 : Câblage dans `app_log.cpp`

- **Tâche :** Remplacer le contenu de `log_message` pour qu'il pousse dans la `LogQueue` plutôt que de verrouiller `g_log_mutex`.

### KPIs, Tests & Baselines

- **Baseline actuelle (À mesurer) :** Créer un test unitaire où 4 threads spamment `LOG_INFO` simultanément. Mesurer la latence maximale d'un appel `LOG_INFO`. Actuellement avec mutex, la contention peut faire monter l'appel à >1 ms.
- **KPI Objectif :** L'appel à `LOG_INFO` doit être constant et **< 1 µs** (temps de formattage en mémoire seul), sans aucune variance liée à l'I/O.
- **Benchmarking :** `just test-logger-stress` (à créer) mesurant le 99th percentile de la latence de `LOG_INFO`.
- **Analyse Tracy :** Le marqueur Tracy sur `LOG_INFO` ne doit plus jamais montrer de blocs d'attente système (`futex_wait`).
- **Probabilité de Succès :** 80%. L'algorithme lock-free MPSC requiert de la rigueur sur le memory ordering (`memory_order_release` / `acquire`), mais est bien maîtrisé.

______________________________________________________________________

## Phase 5.4 : Memory False Sharing Padding

### Concept & Motivations (5.4)

Dans `VulkanEngine`, les variables d'état du Thread I/O (comme `std::atomic<bool> hdrIoThreadRunning`, `hdrLoadInFlight`, `pendingHdrIndex`) vivent probablement sur la même **ligne de cache de 64 octets** que des variables intensément lues par le Main Thread (comme `frameNumber`, `currentHdrIndex`).
Quand le Thread I/O écrit `hdrLoadInFlight = false`, le CPU invalide la ligne de cache L1/L2 entière du P-Core de rendu, provoquant un Cache Miss de pénalité lors de la frame suivante.

### Blocs d'Itérations MVP (5.4)

#### Bloc 1 : Regroupement & Padding

- **Tâche :** Déplacer toutes les variables d'état liées à l'I/O asynchrone dans une structure dédiée `AsyncIoState`.
- **Tâche :** Appliquer le spécificateur d'alignement étendu `alignas(128)` à cette structure pour forcer son isolation sur 2 lignes de cache exclusives (évitant l'effet prefetch du L2).

#### Bloc 2 : Isolation des Atomics du Main Thread

- **Tâche :** Auditer le reste de `VulkanEngine` pour s'assurer qu'aucune autre primitive de synchronisation (`std::atomic`, `VkFence` poolees) ne cohabite avec des données froides.

### Mesures, Tests & Baselines (5.4)

- **Baseline actuelle :** Utilisation de `perf c2c` (Cache-to-Cache) ou `perf stat -e cpu_core/L1-dcache-load-misses/` durant la phase de chargement IBL (appui sur Page Down).
- **KPI Objectif :** Zéro évènement de "False Sharing" détecté par `perf c2c` sur l'adresse de `hdrLoadInFlight`. Diminution marginale du taux global de L1 misses lors du chargement.
- **Benchmarking :** Comparaison stricte des compteurs `perf` avant et après le commit sur le scénario de benchmark.
- **Probabilité de Succès :** 95%. Solution triviale (juste du réarrangement de mémoire et un mot clé C++). Gain modeste mais architecturalement parfait.
