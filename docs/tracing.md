# 🛠️ Tracing & Analyse (RenderDoc)

Cette section documente les outils et procédures utilisés pour valider que la géométrie de l'Icosphere réside exclusivement en VRAM et pour analyser les performances du pipeline.

______________________________________________________________________

## 1. Configuration de l'Environnement

Pour une analyse précise, le binaire doit inclure les symboles de debug et désactiver les optimisations de registre qui masquent les données au profiler.

- **Cible de build** : `just build-debug`
- **Chemin du binaire** : `build/debug/vulkan_app`
- **Lancement** : `just renderdoc_bin=/path/to/qrenderdoc renderdoc`

## 2. Instrumentation du Code (Debug Utils)

Nous utilisons l'extension `VK_EXT_debug_utils` pour sortir les ressources de l'anonymat dans l'interface de RenderDoc.

### A. Naming des Ressources

Chaque objet Vulkan est étiqueté pour une identification rapide dans le **Resource Inspector** :

- **Buffers** : `Icosphere_Vertex_Buffer`, `Icosphere_Index_Buffer`, `Global_MVP_UBO`.
- **Images** : `Depth_Buffer_Image`, `Main_Swapchain`.
- **Pipeline** : `Main_Graphics_Pipeline`.

### B. Régions de Debug (Labels)

Les appels de commandes sont regroupés dans des blocs logiques colorés dans l'**Event Browser** :

- **Render_Icosphere_Pass (Orange)** : Regroupe tout le cycle de rendu d'une frame.
- **GPU_Staging_Copy (Vert)** : Identifie les transferts ponctuels CPU -> GPU.

## 3. Protocole de Validation "Full GPU"

Pour confirmer que l'application utilise correctement la VRAM (Device Local Memory) :

1. **Capture** : Effectuer une capture de frame (F12).
1. **Resource Inspector** : Filtrer avec le mot-clé `ico`.
1. **Memory Check** : Sélectionner `Icosphere_Vertex_Buffer`, cliquer sur l'ID de mémoire associée.
1. **Verdict** : Le type de mémoire doit être **`eResDeviceMemory`** (indiquant `VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT`).

## 4. Analyse du Flux (Timeline)

Dans l'**Event Browser**, nous validons l'ordre des opérations :

1. **EID 3** : Écriture dans la mémoire cohérente pour la matrice MVP.
1. **EID 6-13** : Exécution du bloc orange de rendu.
1. **Present** : Envoi de l'image finale à la Swapchain.
