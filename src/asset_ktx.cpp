#include "asset_ktx.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>

KtxResult ktx2_bake_hdr_to_file(const std::string& outPath, int width, int height, const float* pixels) {
    FILE* f = fopen(outPath.c_str(), "wb");
    if (!f)
        return KtxResult::WriteError;

    KTX2Header header;
    header.pixelWidth = width;
    header.pixelHeight = height;

    // Calcul de la taille des donnees
    uint64_t payloadSize = (uint64_t)width * height * 16; // 4x float

    // Offset payload: Header (80) + LevelIndex (24)
    uint64_t payloadOffset = sizeof(KTX2Header) + sizeof(KTX2LevelIndex);

    KTX2LevelIndex levelIndex;
    levelIndex.byteOffset = payloadOffset;
    levelIndex.byteLength = payloadSize;
    levelIndex.uncompressedByteLength = payloadSize;

    // Ecriture
    fwrite(&header, sizeof(KTX2Header), 1, f);
    fwrite(&levelIndex, sizeof(KTX2LevelIndex), 1, f);
    fwrite(pixels, 1, payloadSize, f);

    fclose(f);
    return KtxResult::Success;
}

KtxResult ktx2_load_from_file(const std::string& inPath, int* outWidth, int* outHeight, const std::function<void*(size_t)>& allocate_func) {
    FILE* f = fopen(inPath.c_str(), "rb");
    if (!f)
        return KtxResult::FileNotFound;

    KTX2Header header;
    if (fread(&header, sizeof(KTX2Header), 1, f) != 1) {
        fclose(f);
        return KtxResult::ReadError;
    }

    if (header.identifier[1] != 0x4B || header.identifier[2] != 0x54) {
        fclose(f);
        return KtxResult::InvalidHeader;
    }

    *outWidth = static_cast<int>(header.pixelWidth);
    *outHeight = static_cast<int>(header.pixelHeight);

    KTX2LevelIndex levelIndex;
    if (fread(&levelIndex, sizeof(KTX2LevelIndex), 1, f) != 1) {
        fclose(f);
        return KtxResult::ReadError;
    }

    fseek(f, static_cast<long>(levelIndex.byteOffset), SEEK_SET);

    void* destBuffer = allocate_func(levelIndex.byteLength);
    if (!destBuffer) {
        fclose(f);
        return KtxResult::ReadError;
    }

    if (fread(destBuffer, 1, levelIndex.byteLength, f) != levelIndex.byteLength) {
        fclose(f);
        return KtxResult::ReadError;
    }

    fclose(f);
    return KtxResult::Success;
}
