#!/usr/bin/env python3
# /// script
# dependencies = [
#   "numpy",
#   "opencv-python",
#   "scikit-image",
# ]
# ///

import os
import sys

import cv2
import numpy as np
from skimage.metrics import structural_similarity as ssim


def compare_hdr(file1, file2):
    try:
        # Read as float32 using OpenCV
        img1 = cv2.imread(file1, cv2.IMREAD_ANYDEPTH | cv2.IMREAD_COLOR)
        img2 = cv2.imread(file2, cv2.IMREAD_ANYDEPTH | cv2.IMREAD_COLOR)

        if img1 is None or img2 is None:
            return "Failed to load image(s)", False

        # Convert BGR to RGB
        img1 = cv2.cvtColor(img1, cv2.COLOR_BGR2RGB)
        img2 = cv2.cvtColor(img2, cv2.COLOR_BGR2RGB)
    except Exception as e:  # noqa: BLE001
        return f"Error loading files: {e}", False

    if img1.shape != img2.shape:
        return f"Shape mismatch: {img1.shape} vs {img2.shape}", False

    # Calculate Mean Squared Error
    mse = np.mean((img1 - img2) ** 2)

    # SSIM requires data_range for floating point images
    # We use the max range across both images to be conservative
    v_min = min(img1.min(), img2.min())
    v_max = max(img1.max(), img2.max())
    drange = v_max - v_min
    if drange <= 1e-5:
        drange = 1.0

    # SSIM requires at least a 7x7 window by default
    h, w = img1.shape[:2]
    if h >= 7 and w >= 7:
        # channel_axis=-1 for RGB/RGBA
        s = ssim(img1, img2, data_range=drange, channel_axis=-1)
    else:
        s = 1.0  # Skip SSIM for tiny maps, rely on MSE

    return {"mse": mse, "ssim": s, "max1": img1.max(), "max2": img2.max()}, True


def main():
    ogl_dir = "/tmp/ibl_tests/ogl"
    vk_dir = "/tmp/ibl_tests/vk"

    files_to_compare = [
        "brdf_lut.hdr",
        "irradiance.hdr",
        "prefiltered_mip0.hdr",
        "prefiltered_mip1.hdr",
        "prefiltered_mip5.hdr",
        "prefiltered_mip10.hdr",
    ]

    print(f"\n{'File':<25} | {'MSE':<12} | {'SSIM':<10} | {'Status'}")
    print("-" * 65)

    all_passed = True
    for f in files_to_compare:
        p1 = os.path.join(ogl_dir, f)
        p2 = os.path.join(vk_dir, f)

        if not os.path.exists(p1) or not os.path.exists(p2):
            status = "MISSING"
            print(f"{f:<25} | {'N/A':<12} | {'N/A':<10} | {status}")
            all_passed = False
            continue

        res, ok = compare_hdr(p1, p2)
        if not ok:
            print(f"{f:<25} | {'ERROR':<12} | {'N/A':<10} | {res}")
            all_passed = False
        else:
            mse = res["mse"]
            ssim_val = res["ssim"]
            max1 = res["max1"]
            max2 = res["max2"]

            # Tolerances:
            # - BRDF LUT: Should be nearly 0 (procedural)
            # - Others: Monte Carlo integrations (1024 samples) have natural noise/variance
            #   MSE < 0.05 (5%) is excellent for HDR certification.
            is_brdf = f == "brdf_lut.hdr"
            is_mip10 = f == "prefiltered_mip10.hdr"
            mse_threshold = 1e-4 if is_brdf else (0.15 if is_mip10 else 0.05)
            ssim_threshold = 0.999 if is_brdf else (0.95 if not is_mip10 else 0.90)

            status = (
                "PASS" if mse < mse_threshold and ssim_val >= ssim_threshold else "FAIL"
            )
            if status == "FAIL":
                all_passed = False
            print(
                f"{f:<25} | {mse:<12.6f} | {ssim_val:<10.4f} | {status} (Max: {max1:.2f} vs {max2:.2f})"
            )

    if all_passed:
        print("\n✅ SUCCESS: Vulkan IBL maps match OGL reference.")
        sys.exit(0)
    else:
        print("\n❌ FAILURE: Significant mismatch detected in IBL maps.")
        sys.exit(1)


if __name__ == "__main__":
    main()
