# Configuration et Règles de Suppression AddressSanitizer / LeakSanitizer (`.asan_ignorefile`)

Date : 16 août 2026\
Statut : Actif / Référence Architecture & CI

______________________________________________________________________

## 1. Contexte et Rôle du Fichier `.asan_ignorefile`

Lors de l'exécution des binaires sous **AddressSanitizer (ASan)** et **LeakSanitizer (LSan)** (`-fsanitize=address,undefined`), le détecteur de fuites mémoire examine l'ensemble des allocations non libérées au terme du processus.

Certaines bibliothèques tierces (pilotes GPU système, loaders X11/XCB, runtimes des compilateurs) réalisent des allocations globales statiques ou "one-shot" qui ne sont volontairement pas désallouées à la terminaison du processus (comportement *leak-by-design* ou structures du serveur d'affichage X11).

Le fichier `.asan_ignorefile` à la racine du dépôt est transmis à LSan via la variable d'environnement :

```bash
export LSAN_OPTIONS="suppressions=.asan_ignorefile"
```

______________________________________________________________________

## 2. Inventaire Détaillé des Règles Actives

```ini
# AddressSanitizer suppression file for Vulkan SDK and driver leaks
# These are known minor leaks in libvulkan.so that don't affect application correctness
# and are not related to user code issues.

# Vulkan SDK/driver initialization and loader leaks
leak:libvulkan.so
leak:libxcb.so
leak:libX11.so

# GCC/Clang UBSan internal vtable/RTTI cache allocations across dlopen/dlclose
leak:libubsan.so
leak:__ubsan
leak:__ubsan_vptr_type_cache
leak:ubsan
```

______________________________________________________________________

## 3. Justification Technique par Composant

### A. Bibliothèques Système et Pilotes (`libvulkan.so`, `libxcb.so`, `libX11.so`)

- **Mécanisme** :
  - `libX11.so` et `libxcb.so` allouent des tables de correspondances de fenêtres et des listes d'extensions lors de `XOpenDisplay` / `glfwInit`. Ces structures sont gérées par la couche Xlib et ne sont pas libérées de manière unitaire avant la sortie standard du processus.
  - `libvulkan.so` (Khronos Loader) alloue des trampolines de couches de validation (`VK_LAYER_KHRONOS_validation`) et des métadonnées de couches ICD lors du scan initial des pilotes.
- **Règle de sécurité** :
  - Ne cible que les bibliothèques partagées dynamiques externes au projet.

### B. Runtime UndefinedBehaviorSanitizer (`libubsan.so`, `__ubsan_vptr_type_cache`)

- **Mécanisme** :
  - Lors de l'activation conjointe de `-fsanitize=undefined` et `-fsanitize=address`, le runtime UBSan maintient une table de hachage interne pour le cache des types vtable/RTTI (`__ubsan_vptr_type_cache`).
  - Lorsque l'application recharge dynamiquement le module RHI via `dlopen()` / `dlclose()` (\[`src/module_loader.cpp`\](file:///home/latty/Prog/__PERSO__/suckless-vulkan/src/module_loader.cpp)), des invocations de méthodes virtuelles RHI provoquent des entrées de cache dans `libubsan.so`.
  - Lors du `dlclose()`, la table globale UBSan n'est pas vidée par le runtime du compilateur, générant des rapports de fuite de 32 à 96 octets ("leak in unknown module" ou `__ubsan`).
- **Règle de sécurité** :
  - Neutralise strictement le cache interne du compilateur sans masquer de code applicatif.

______________________________________________________________________

## 4. Règle Stricte de Non-Suppression Applicative

> [!CAUTION]
> **Interdiction Formelle** : Aucune bibliothèque ni module produit par le dépôt (`libvulkan_rhi.so`, `libnull_rhi.so`, `vulkan_app`, `unit_tests`) ne doit figurer dans `.asan_ignorefile`.
>
> Ajouter `leak:libvulkan_rhi.so` ou un symbole interne supprime la détection des fuites de notre propre moteur (VMA, swapchains, descripteurs). Toutes les fuites applicatives doivent être corrigées par une gestion RAII / destruction explicite dans le code C++.
