# Architecture de Threading et Affinité CPU (Pinning)

L'optimisation des caches de bas niveau (L2/L3) dans une architecture moderne hybride (P-Cores / E-Cores) nécessite de prendre le contrôle manuel de la répartition des threads (Thread Affinity). L'objectif est d'empêcher l'OS (via son gouverneur d'énergie) de migrer un thread critique d'un cœur à un autre, ce qui invalide le cache L2 et provoque des pénalités dramatiques de transfert inter-caches.

## ⚠️ Limites de Portabilité Actuelles

L'implémentation de validation (Itération 7.2) utilise `pthread_setaffinity_np` et `CPU_SET(0)` pour verrouiller le thread principal sur le P-Core 0. Cette approche "Caveman" prouve l'efficacité du concept (baisse de 61% des misses L2, 0 migration), mais présente des limitations de portabilité strictes :

### 1. Dépendance à l'OS (Operating System)

La fonction `pthread_setaffinity_np` porte le suffixe `_np` pour **Non-Portable**. Elle est exclusive aux systèmes GNU/Linux.

- **Windows :** Il est impératif d'abstraire le code pour utiliser l'API Win32 `SetThreadAffinityMask()`.
- **macOS :** Apple interdit l'affinité CPU stricte pour laisser son noyau XNU gérer l'énergie. L'API POSIX n'y expose pas `pthread_setaffinity_np`.

### 2. Dépendance à la Topologie Matérielle (Hardcodage)

L'appel `CPU_SET(0)` hardcode l'index `0`.

- Sur notre machine de développement (Intel x86_64 Raptor Lake), le cœur logique `0` est un **P-Core** physique.
- Sur d'autres topologies (ARM big.LITTLE, AMD asymétriques, ou autres puces Intel), l'index `0` pourrait pointer vers un E-Core (économique) ou un simple thread SMT. Verrouiller le moteur de rendu sur un E-Core détruirait les performances. L'index d'un cœur ne garantit pas sa nature physique.

## 🛠️ Solution Architecturale Cible (Cross-Platform)

Pour que le moteur maintienne son isolation L2 sans se briser sur d'autres PC, la gestion des threads doit évoluer vers une couche d'abstraction :

1. **Détection au Runtime (`hwloc`) :**
   Intégration de la librairie **Hardware Locality (`hwloc`)** pour cartographier la hiérarchie du CPU (caches L2 partagés, L3, distinction P-Cores/E-Cores) de manière dynamique au lancement du moteur.
1. **Couche d'Abstraction OS :**
   Création d'une interface de Threading interne (ex: `os_bind_thread_to_pcore(ThreadID)`). Cette fonction devra :
   - Interroger `hwloc` pour récupérer le vrai masque binaire du meilleur P-Core physique disponible.
   - Appeler `pthread_setaffinity_np` sous Linux.
   - Appeler `SetThreadAffinityMask` sous Windows.
   - Ignorer gracieusement l'opération sous macOS en loggant un warning.
