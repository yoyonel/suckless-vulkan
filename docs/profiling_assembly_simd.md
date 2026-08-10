# Autopsie Binaire et Optimisation SIMD

Ce document référence la méthodologie employée (notamment lors de l'Itération 7.5) pour analyser l'assembleur généré par le compilateur et forcer l'utilisation d'instructions matérielles avancées (SIMD : Single Instruction, Multiple Data).

## 1. Méthodologie d'Analyse (Désassemblage)

Pour vérifier si le compilateur a bien vectorisé une boucle critique, il ne faut jamais deviner : il faut lire l'assembleur final.

**Commande type (Linux / GCC / Clang) :**

```bash
objdump -d build/release/libvulkan_rhi.so | awk '/<.*mon_nom_de_fonction.*>:/, /^$/'
```

*Astuce : On peut chainer avec un `grep -E "vfmadd|vmovaps|ymm"` pour chercher spécifiquement les instructions larges.*

### Ce qu'on cherche (sur architecture x86_64)

- **Le Scalaire (Mauvais pour la performance brute) :** Registres `xmm` (128-bit utilisés partiellement), instructions se terminant par `ss` (Scalar Single-precision) comme `addss`, `mulss`.
- **Le Vectoriel (Bon, AVX/AVX2) :** Registres `ymm` (256-bit, capables de stocker 8 floats), instructions `vmovaps` (Aligned Packed Single), `vfmadd231ss` (Fused Multiply-Add).

## 2. Forcer la Vectorisation au scalpel

Plutôt que d'activer le flag `-mavx2` sur tout le projet CMake (ce qui ferait crasher l'exécutable sur les vieux CPU qui n'ont pas l'instruction), il est plus sûr d'optimiser localement la fonction critique.

### L'approche hybride (Cross-Platform) : Unroll + Target Attribute

> ⚠️ **ATTENTION :** `__attribute__` est une extension spécifique à GCC/Clang. Sous Windows avec MSVC, le compilateur refusera de compiler. Il **faut impérativement** utiliser des macros de préprocesseur pour isoler ces optimisations.

```cpp
// Définition propre dans un header (ex: core_engine.h)
#if defined(__GNUC__) || defined(__clang__)
#  define FORCE_AVX2 __attribute__((target("avx2,fma")))
#  define PRAGMA_UNROLL_4 _Pragma("GCC unroll 4")
#else
// Sous MSVC (Windows), l'unroll se gère avec un pragma différent, et l'AVX se met dans les flags du projet CMake.
#  define FORCE_AVX2
#  define PRAGMA_UNROLL_4 __pragma(loop(unroll, 4))
#endif

// 1. On force la cible d'architecture (sans casser MSVC)
FORCE_AVX2
void ma_boucle_critique() {
    
    // 2. On indique au compilo que nos pointeurs sont alignés en mémoire
    // (Sur MSVC, __builtin_assume_aligned devient souvent inutile ou se remplace par __assume)
    const float* __restrict data = static_cast<const float*>(__builtin_assume_aligned(ptr, 64));
    
    // 3. On déroule la boucle de façon portable
    PRAGMA_UNROLL_4
    for (int i = 0; i < count; ++i) {
        // ... calcul lourd
    }
}
```

## 3. Dépendances : OS, Compilateur et Architecture

Ces techniques de bas niveau sont **extrêmement dépendantes** de la plateforme :

### A. Dépendance au Compilateur

- `__attribute__((target(...)))` et `#pragma GCC unroll` sont spécifiques à **GCC** et **Clang**.
- Sous Windows avec **MSVC**, il faudrait utiliser `#pragma loop(unroll, 4)` ou gérer la vectorisation globale via les flags du projet (`/arch:AVX2`).

### B. Dépendance à l'Architecture (CPU)

- Les registres `ymm`, les instructions AVX2, et le FMA n'existent que sur les architectures **x86_64** (Intel/AMD modernes).
- Sur un processeur **ARM** (Raspberry Pi, Apple Silicon M1/M2/M3), l'assembleur sera radicalement différent. L'équivalent AVX est l'extension **NEON**. Vous y verrez des registres `q0`-`q15` et des instructions comme `fmla` (Floating-point Multiply-Accumulate). Le compilateur l'utilisera si on compile avec les flags natifs ARM.

### C. Dépendance à l'OS (Outils)

- **Linux** : `objdump` (GNU Binutils) est natif.
- **macOS** : L'outil binaire GNU `objdump` est remplacé par `otool` ou `llvm-objdump`.
- **Windows** : Le désassembleur officiel de Microsoft est `dumpbin /DISASM` (livré avec Visual Studio).

## 4. L'Écueil de l'Assembleur Inline (Inline ASM)

Il peut être tentant d'écrire l'assembleur SIMD directement dans le code source via des blocs `__asm__` (comme cela se faisait couramment en C). **C'est une pratique formellement déconseillée dans ce moteur**, pour trois raisons critiques :

1. **Incompatibilité Windows/MSVC 64-bit :** Le compilateur Microsoft (MSVC) pour cibles x64 **interdit** purement et simplement le mot-clé `__asm`. Si l'architecture contient de l'inline ASM, le portage Windows nécessitera de déporter le code dans des fichiers `.asm` compilés via MASM, un cauchemar de maintenance.
1. **Casse l'optimiseur (Black Box) :** Lorsqu'un compilateur moderne (GCC/Clang) rencontre un bloc d'assembleur inline, il le traite comme une "boîte noire". Il vide les registres, désactive ses propres optimisations de réagencement (Instruction Scheduling), et perd la visibilité sur les dépendances de données.
1. **L'alternative supérieure (Intrinsics) :** Si un contrôle manuel absolu est requis, il faut utiliser les **Intrinsics C** (`<immintrin.h>`, ex: `_mm256_add_ps`). Ils sont 100% portables (reconnus par GCC, Clang, MSVC) et laissent le compilateur allouer intelligemment les registres physiques (`ymm0`, `ymm1`, etc.).

Dans >95% des cas, l'approche *Data-Oriented* moderne (Structures alignées type SoA + `#pragma unroll` + attributs de compilation) produit un code binaire identique, voire supérieur à l'assemblage manuel, car le compilateur a une vue d'ensemble du pipeline.

## 5. Conclusion

L'utilisation de ces techniques d'optimisation de bas niveau doit rester exceptionnelle et cantonnée aux **Hot Paths** (les boucles exécutées des milliers de fois par frame). L'Itération 7.5 a démontré qu'un simple `pragma` et `attribute` bien placés permettaient de gratter ~12% de temps CPU sans avoir à écrire de code manuel complexe, tout en préservant un code C++ lisible et cross-platform.
