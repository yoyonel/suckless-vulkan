# Guide d'Architecture & Documentation Technique : Dynamic Descriptor Offsets & Unified UBO Ring Buffering (Levier 3)

**Date :** 16 Août 2026\
**Auteur :** Antigravity Engine Architecture\
**Branche :** `feature/render-graph-integration`

______________________________________________________________________

## 1. Raison d'Être & Problématique Fondamentale

### 1.1 Le Modèle d'Exécution Asynchrone de Vulkan

Dans une architecture de rendu moderne Vulkan, le CPU et le GPU fonctionnent en pipeline asynchrone découplé :

- **Pendant que le GPU exécute la frame $N$**, le **CPU prépare déjà et enregistre la frame $N+1$**.
- Plusieurs frames (généralement 2 à 3) sont dites « en vol » (*frames-in-flight*) simultanément.

```text
Timeline :
CPU : |-- Frame N --|-- Frame N+1 (Écriture UBO) --|-- Frame N+2 --|
GPU :                |------ Frame N (Lecture UBO) ------|------ Frame N+1 ------|
                                      ▲
                         DATA RACE INITIAL DÉTECTÉ !
```

### 1.2 Le Risque de Data Race Initial (Avant Levier 3)

Avant l'implémentation du Levier 3, l'application allouait un buffer uniforme global unique de taille exacte `sizeof(UBOData)` (448 octets).

- À chaque frame, la méthode `UpdateUBO()` effectuait un `memcpy` direct dans la zone mémoire mappée.
- **Problème critique** : Dès que le GPU avait un léger retard de tracé sur la frame $N$, le CPU écrasait les données de matrices MVP de la frame $N+1$ à la même adresse mémoire alors que les vertex shaders de la frame $N$ lisaient encore ce buffer.
- **Conséquences** : Tearing géométrique, artefacts visuels de caméra (jittering), ou corruption d'éclairage aléatoire à haute charge.

______________________________________________________________________

## 2. Analyse des Alternatives Rejetées

Pour résoudre cette course aux données, deux approches naïves ont été évaluées et rejetées :

### ❌ Alternative 1 : Synchronisation Bloquante CPU-GPU (`vkQueueWaitIdle` ou Fences par frame)

- **Principe** : Forcer le CPU à attendre que le GPU ait fini de lire l'UBO avant d'écrire la frame suivante.
- **Pourquoi c'est rejeté** : Cela détruit tout parallélisme matériel. Le CPU et le GPU travaillent en série au lieu d'en parallèle, ce qui peut réduire le framerate de moitié (perte de débit de 50%).

### ❌ Alternative 2 : Allocation Multi-Descripteurs & `vkUpdateDescriptorSets` par frame

- **Principe** : Allouer 3 UBOs séparés, 3 Descriptor Sets distincts, et mettre à jour le Descriptor Set à chaque frame.
- **Pourquoi c'est rejeté** :
  1. Génère un overhead CPU continu lié aux appels `vkUpdateDescriptorSets`.
  1. **Invalide les Command Buffers secondaires du Levier 1** : Les Command Buffers secondaires pré-enregistrés nécessitent des handles de descripteurs et des états stables pour être réutilisés sans ré-enregistrement.

______________________________________________________________________

## 3. Architecture Retenue : Triple-Buffering & Dynamic Descriptor Offsets

La solution industrielle Vulkan standard repose sur la combinaison de **Ring Buffering** et de **Dynamic Descriptor Offsets**.

```mermaid
graph TD
    subgraph Ring Buffer Mémoire Host VMA (1536 octets)
        Slot0["Slot 0 (Offset 0x000, 512B) - Frame N"]
        Slot1["Slot 1 (Offset 0x200, 512B) - Frame N+1"]
        Slot2["Slot 2 (Offset 0x400, 512B) - Frame N+2"]
    end

    subgraph Descriptor Set Global (Écrit 1 seule fois au Boot)
        DS["VkDescriptorSet (Binding 0: VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, range: 448B)"]
    end

    subgraph Trio de Command Buffers Secondaires Statiques
        SCB0["Secondary CB 0 (pré-enregistré avec offset 0x000)"]
        SCB1["Secondary CB 1 (pré-enregistré avec offset 0x200)"]
        SCB2["Secondary CB 2 (pré-enregistré avec offset 0x400)"]
    end

    DS --> Slot0
    DS --> Slot1
    DS --> Slot2

    SCB0 -.->|Lie au Draw| Slot0
    SCB1 -.->|Lie au Draw| Slot1
    SCB2 -.->|Lie au Draw| Slot2
```

### 3.1 Ring Buffer UBO Triple-Bufferisé

- **Calcul du Stride Matériel Aligné** :

  ```cpp
  constexpr size_t kUboStride = (sizeof(UBOData) + 255) & ~255; // 512 octets
  constexpr size_t kRingBufferFrames = 3;
  // Allocation totale : 512 * 3 = 1536 octets
  ```

  Le stride de 512 octets respecte la contrainte matérielle `minUniformBufferOffsetAlignment` (typiquement 64 ou 256 octets sur Intel Iris Xe / x86_64).

- **Écriture sans verrou ni synchronisation bloquante** :

  ```cpp
  void VulkanRHI::UpdateUBO(const UBOData& data) {
      constexpr size_t kUboStride = (sizeof(UBOData) + 255) & ~255;
      uint32_t slot = _engine->currentFrameIndex % 3;
      uint8_t* dst = static_cast<uint8_t*>(_engine->uniformBufferMapped) + (slot * kUboStride);
      memcpy(dst, &data, sizeof(UBOData));
  }
  ```

### 3.2 Descripteur Dynamique Write-Once (`VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC`)

- **Au Boot** : Le Descriptor Set global est alloué et écrit **une seule fois** :

  - Type : `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC`
  - Range : `sizeof(UBOData)` (448 octets)
  - Offset de base : `0`

- **En Boucle de Rendu** : Zéro appel à `vkUpdateDescriptorSets`. Le slot actif est sélectionné directement lors de l'appel `vkCmdBindDescriptorSets` en passant un offset dynamique (4 octets) :

  ```cpp
  uint32_t dynamicOffset = static_cast<uint32_t>((_engine->currentFrameIndex % 3) * kUboStride);
  vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 1, &dynamicOffset);
  ```

### 3.3 Préservation 100% du Levier 1 (Trio de Secondary Command Buffers)

Pour que les Command Buffers secondaires restent **parfaitement statiques et réutilisables** sans jamais être ré-enregistrés en cours de rendu :

- On alloue 3 Command Buffers secondaires (`secondaryForwardCb[0..2]`).
- Lors de l'initialisation / première frame, chaque buffer est pré-enregistré avec son offset dynamique fixe respectif (`slot * 512`).
- À chaque frame $N$, le Command Buffer primaire exécute directement `secondaryForwardCb[currentFrameIndex % 3]`.

______________________________________________________________________

## 4. Bilan et Analyse des Performances

### 4.1 Pourquoi les Profilers CPU / DRAM ne montrent pas de "Vitesse Brute" Additionnelle ?

- **Levier 1** avait déjà éliminé le coût CPU d'enregistrement des draw calls (réduction de $39.5%$ des instructions).
- **Levier 2** avait déjà éliminé la saturation DRAM lors des I/O textures (réduction de $88.0%$ de saturation bande passante).
- **Levier 3** apporte un gain de **robustesse de synchronisation et de parallélisme GPU-CPU** :
  - Zéro stall GPU sur buffer partagé.
  - Zéro corruption de matrice en vol.
  - Zéro allocation ni réécriture de descripteur.

### 4.2 Tableau Récapitulatif Multi-Leviers

| Métrique Matérielle | Baseline Initiale | Levier 1 (Sec CB) | Levier 2 (Chunk KTX2) | Levier 3 (Dynamic UBO Ring) | Statut Global |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Temps CPU Global (VTune)** | $10.09\\text{ s}$ | $8.45\\text{ s}$ | $5.99\\text{ s}$ | **$6.17\\text{ s}$** | 🟢 **$-38.8%$** |
| **LLC Miss Count (VTune)** | $1,950,819$ | $1,820,000$ | $1,300,546$ | **$1,300,546$** | 🟢 **$-33.3%$** |
| **Saturation DRAM Bandwidth** | $25.1%$ | $24.8%$ | $3.0%$ | **$3.0%$** | 🟢 **$-88.0%$** |
| **Instructions CPU (Callgrind)** | $223.0\\text{ M Ir}$ | **$134.8\\text{ M Ir}$** | $135.0\\text{ M Ir}$ | **$135.2\\text{ M Ir}$** | 🟢 **$-39.4%$** |
| **Course aux données UBO** | Présente (1 UBO) | Présente (1 UBO) | Présente (1 UBO) | **Éliminée (Triple Ring)** | 🟢 **Thread-Safe** |
| **Descriptor Churn / Frame** | $0$ (statique) | $0$ | $0$ | **$0$ (Dynamic Offsets)** | 🟢 **Write-Once** |

______________________________________________________________________

## 5. Guide pour les Développeurs Futurs

1. **Modification de `UBOData`** :
   - Si des champs sont ajoutés ou modifiés dans `struct UBOData`, s'assurer que `sizeof(UBOData)` reste inférieur ou égal à `kUboStride` (512 octets).
   - Si `sizeof(UBOData) > 512`, mettre à jour `kUboStride` avec la formule d'alignement `(sizeof(UBOData) + 255) & ~255`.
1. **Ajout d'un nouvel UBO dynamique** :
   - Toujours préférer `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC` à des ensembles multiples pour les données modifiées par frame.
   - Ne jamais appeler `vkUpdateDescriptorSets` dans la boucle principale de rendu.
