#!/usr/bin/env python3
"""
RHI (Render Hardware Interface) Leak Checker.

Motivation:
L'architecture RHI exige une séparation stricte entre la logique applicative (CoreEngine)
et l'API graphique native (Vulkan). Ce script garantit qu'aucune dépendance Vulkan (types,
includes) ne "fuite" dans les fichiers de logique pure. Cela permet de compiler le moteur
en mode headless (sans GPU) pour les tests unitaires et de préparer le terrain pour
d'autres backends (ex: NullRHI, DirectX).
"""

import os
import re
import sys

FILES_TO_CHECK = [
    "src/core_engine.h",
    "src/core_engine.cpp",
]

# Patterns recherchés pour identifier les fuites d'API Vulkan.
FORBIDDEN_PATTERNS = [
    r"#include\s+<vulkan/vulkan\.h>",  # Interdit l'inclusion directe de l'en-tête Vulkan
    r"\bVk[A-Z]\w+",  # Capture les types Vulkan standards (ex: VkBuffer, VkImage, VkDevice)
    r"\bvma[A-Z]\w+",  # Capture les fonctions VMA (ex: vmaCreateBuffer)
    r"\bVma[A-Z]\w+",  # Capture les types VMA (ex: VmaAllocator, VmaAllocation)
]


def check_file(filepath):
    if not os.path.exists(filepath):
        return True

    with open(filepath, "r", encoding="utf-8") as f:
        content = f.read()

    failed = False
    for pattern in FORBIDDEN_PATTERNS:
        matches = re.finditer(pattern, content)
        for match in matches:
            line_no = content.count("\n", 0, match.start()) + 1
            print(
                f"ERROR: RHI Leak found in {filepath}:{line_no} -> '{match.group(0)}'"
            )
            failed = True
    return not failed


if __name__ == "__main__":
    all_passed = True
    files_to_check = sys.argv[1:] if len(sys.argv) > 1 else FILES_TO_CHECK
    for file in files_to_check:
        if not check_file(file):
            all_passed = False

    if not all_passed:
        print("RHI Leak Check FAILED. Pure logic files must not contain Vulkan types.")
        sys.exit(1)

    print("RHI Leak Check PASSED.")
    sys.exit(0)
