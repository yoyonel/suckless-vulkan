# Analyse : Abstraction RHI (Render Hardware Interface) & NullRHI

**Date :** 2026-08-07

## 1. Le Concept

L'objectif d'une architecture RHI est de ne **jamais** appeler d'API graphique native (Vulkan, DirectX, VMA) directement dans la logique métier du moteur (Caméra, Scène, Physique, Input).
Tout le code natif est isolé derrière une interface C++ abstraite (ex: `IRenderHardwareInterface`).

À partir de cette interface, on implémente des backends spécifiques :

- `VulkanRHI`
- `DirectX12RHI`
- **`NullRHI`** : Une implémentation factice. Ses fonctions (ex: `CreateBuffer`, `Draw`) ne font strictement rien et retournent simplement un code de succès.

### Avantages

- **Tests CI "Headless" (Sans GPU) :** Les serveurs d'intégration continue (comme Github Actions) n'ont pas de GPU Vulkan. Le `NullRHI` permet de lancer le moteur, charger une scène 3D, et tester la logique : le moteur "croit" qu'il dessine, mais le `NullRHI` ignore silencieusement les commandes.
- **Mocking Avancé :** Le `NullRHI` peut compter les requêtes (ex: "J'ai reçu 45 DrawCalls") pour vérifier le bon fonctionnement du *culling* dans des tests unitaires rapides.
- **Portabilité :** Le portage vers d'autres plateformes (Mac/Metal, Consoles) n'impacte jamais le code métier.

### Inconvénients

- **Boilerplate Massif :** Nécessité de créer des structures génériques (`SVK_Buffer`, `SVK_Texture`) pour masquer la nature des `VkBuffer` et `VkImage` au reste de l'application.

### Références Industrielles

- **Unreal Engine 5** : L'option `-nullrhi` permet de démarrer un serveur multijoueur dédié (qui a besoin de charger la géométrie pour les collisions) sans carte graphique.
- **O3DE** : Open 3D Engine utilise un RHI massif pour unifier les APIs.
- **NVRHI** : Librairie open-source de NVIDIA montrant comment abstraire Vulkan/DX12 ([GitHub NVRHI](https://github.com/NVIDIAGameWorks/nvrhi)).

______________________________________________________________________

## 2. Transition pour Suckless-Vulkan (Séparation Logique / Rendu)

### État actuel ("God Object")

Actuellement, `VulkanEngine` centralise tout. Il gère la fenêtre (GLFW), l'Input, la Caméra, la boucle applicative, mais aussi les commandes natives Vulkan, la Swapchain, et VMA. L'API métier et l'API de rendu sont totalement intriquées.

### Évaluation du Coût de Séparation

- **Temps estimé :** 2 à 3 semaines à temps plein de refactoring profond.
- **Difficulté :** Élevée. Il faut abstraire des structures très couplées comme `Material` ou `AllocatedImage` pour qu'elles ne contiennent plus de pointeurs Vulkan explicites.

### Comment implémenter `VulkanRHI.cpp` proprement et simplement ?

Pour éviter l'enfer de l'héritage C++ (créer des classes virtuelles `IBuffer`, `ITexture`, etc.), l'approche la plus moderne, performante et *clean* est le **Système de Handles (IDs opaques)**.

**1. L'Engine Applicatif (`CoreEngine`)**
Il ne connait rien de Vulkan (zéro `#include <vulkan/vulkan.h>`). Il manipule uniquement des entiers (`Handle`).

```cpp
using MeshHandle = uint32_t;
using TextureHandle = uint32_t;

class CoreEngine {
    IRHI* rhi;
    void Init() {
        MeshHandle myMesh = rhi->CreateMesh(vertices, indices);
    }
    void Draw() {
        rhi->BindMesh(myMesh);
        rhi->Draw();
    }
};
```

**2. L'Interface (`IRHI.h`)**
Une interface très resserrée (Facade).

```cpp
class IRHI {
public:
    virtual ~IRHI() = default;
    virtual MeshHandle CreateMesh(const std::vector<Vertex>& vertices) = 0;
    virtual void BindMesh(MeshHandle handle) = 0;
    virtual void Draw() = 0;
};
```

**3. L'implémentation (`VulkanRHI.cpp`)**
C'est ici qu'on stocke le vrai code. La classe maintient des conteneurs (tableaux ou maps) qui associent le `Handle` (ID) aux vrais objets Vulkan.

```cpp
class VulkanRHI : public IRHI {
private:
    struct VulkanMesh {
        VkBuffer vertexBuffer;
        VmaAllocation allocation;
    };
    std::vector<VulkanMesh> _meshes; // Le Handle (ID) sert d'index dans ce tableau

public:
    MeshHandle CreateMesh(const std::vector<Vertex>& vertices) override {
        VulkanMesh mesh;
        // ... Code actuel de vkCreateBuffer et vmaCreateBuffer ...
        _meshes.push_back(mesh);
        return _meshes.size() - 1; // Retourne l'ID
    }
};
```

### Bénéfices de l'approche par Handles

1. **Zéro Virtual Overhead interne :** On n'a qu'une seule interface virtuelle (`IRHI`). Les buffers et textures ne sont pas des objets virtuels alloués dynamiquement.
1. **Data-Oriented Design :** Les objets Vulkan restent groupés dans des tableaux denses dans `VulkanRHI.cpp`, maximisant les performances du cache CPU.
1. **Sécurité :** L'Engine ne peut pas manipuler ou détruire accidentellement un pointeur Vulkan, il ne possède qu'un simple ID.
