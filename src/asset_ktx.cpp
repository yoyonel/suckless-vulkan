#include "asset_ktx.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>

namespace {
constexpr size_t kStreamingChunkSize = 64ULL * 1024ULL; // 64 KB chunk size (matches CPU cache line streaming)
} // namespace

KtxResult ktx2_bake_hdr_to_file(const std::string& outPath, int width, int height, const float* pixels) {
    FILE* f = fopen(outPath.c_str(), "wb");
    if (!f)
        return KtxResult::WriteError;

    int fd = fileno(f);
    if (fd >= 0) {
        posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
    }

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
    if (fwrite(&header, sizeof(KTX2Header), 1, f) != 1 || fwrite(&levelIndex, sizeof(KTX2LevelIndex), 1, f) != 1) {
        fclose(f);
        return KtxResult::WriteError;
    }

    const uint8_t* bytePtr = reinterpret_cast<const uint8_t*>(pixels);
    size_t remaining = payloadSize;
    while (remaining > 0) {
        size_t toWrite = (remaining < kStreamingChunkSize) ? remaining : kStreamingChunkSize;
        if (fwrite(bytePtr, 1, toWrite, f) != toWrite) {
            fclose(f);
            return KtxResult::WriteError;
        }
        bytePtr += toWrite;
        remaining -= toWrite;
    }

    fclose(f);
    return KtxResult::Success;
}

KtxResult ktx2_load_from_file(const std::string& inPath, int* outWidth, int* outHeight, const std::function<void*(size_t)>& allocate_func) {
    FILE* f = fopen(inPath.c_str(), "rb");
    if (!f)
        return KtxResult::FileNotFound;

    int fd = fileno(f);
    if (fd >= 0) {
        posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
    }

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

    if (fseek(f, static_cast<long>(levelIndex.byteOffset), SEEK_SET) != 0) {
        fclose(f);
        return KtxResult::ReadError;
    }

    void* destBuffer = allocate_func(levelIndex.byteLength);
    if (!destBuffer) {
        fclose(f);
        return KtxResult::ReadError;
    }

    uint8_t* outPtr = static_cast<uint8_t*>(destBuffer);
    size_t remaining = levelIndex.byteLength;
    while (remaining > 0) {
        size_t toRead = (remaining < kStreamingChunkSize) ? remaining : kStreamingChunkSize;
        if (fread(outPtr, 1, toRead, f) != toRead) {
            fclose(f);
            return KtxResult::ReadError;
        }
        outPtr += toRead;
        remaining -= toRead;
    }

    fclose(f);
    return KtxResult::Success;
}
