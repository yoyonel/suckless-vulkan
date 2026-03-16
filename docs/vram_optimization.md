# Optimisation VRAM : Staging Buffers

Pour obtenir les meilleures performances, la géométrie doit résider dans la mémoire **Device Local** du GPU. Cette mémoire est physiquement proche des unités de calcul mais est inaccessible directement par le CPU.

## 📥 Le concept de Staging

Comme le CPU ne peut pas écrire directement dans la VRAM rapide, nous utilisons un "Staging Buffer" (tampon de transfert) :

1. **Allocation Source :** Un buffer hôte (CPU) est créé avec `VMA_MEMORY_USAGE_CPU_ONLY`.
1. **Transfert :** Une commande de copie (`vkCmdCopyBuffer`) est enregistrée et soumise à la file de transfert du GPU.
1. **Allocation Cible :** Le buffer final est créé avec `VMA_MEMORY_USAGE_GPU_ONLY`.

## 🚀 Avantages

- **Bande passante :** Le GPU lit ses données à sa vitesse native (plusieurs centaines de Go/s).
- **Fragmentation :** VMA gère ces allocations complexes pour nous, évitant les trous de mémoire en VRAM.
