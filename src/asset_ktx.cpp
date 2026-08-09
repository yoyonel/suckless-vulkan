#include "asset_ktx.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>

bool ktx2_bake_hdr_to_file(const std::string& outPath, int width, int height, const float* pixels) {
    FILE* f = fopen(outPath.c_str(), "wb");
    if (!f)
        return false;

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
    return true;
}

float* ktx2_load_from_file(const std::string& inPath, int* outWidth, int* outHeight) {
    FILE* f = fopen(inPath.c_str(), "rb");
    if (!f)
        return nullptr;

    KTX2Header header;
    if (fread(&header, sizeof(KTX2Header), 1, f) != 1) {
        fclose(f);
        return nullptr;
    }

    // Verif magic simpliste
    if (header.identifier[1] != 0x4B || header.identifier[2] != 0x54) { // 'K', 'T'
        fclose(f);
        return nullptr;
    }

    *outWidth = static_cast<int>(header.pixelWidth);
    *outHeight = static_cast<int>(header.pixelHeight);

    KTX2LevelIndex levelIndex;
    if (fread(&levelIndex, sizeof(KTX2LevelIndex), 1, f) != 1) {
        fclose(f);
        return nullptr;
    }

    fseek(f, static_cast<long>(levelIndex.byteOffset), SEEK_SET);

    float* data = (float*)malloc(levelIndex.byteLength);
    if (fread(data, 1, levelIndex.byteLength, f) != levelIndex.byteLength) {
        free(data);
        fclose(f);
        return nullptr;
    }

    fclose(f);
    return data;
}
