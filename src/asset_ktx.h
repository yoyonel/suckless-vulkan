#ifndef ASSET_KTX_H
#define ASSET_KTX_H

#include <cstdint>
#include <string>
#include <vector>

#pragma pack(push, 1)
struct KTX2Header {
    uint8_t identifier[12] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
    uint32_t vkFormat = 109; // VK_FORMAT_R32G32B32A32_SFLOAT
    uint32_t typeSize = 16;
    uint32_t pixelWidth = 0;
    uint32_t pixelHeight = 0;
    uint32_t pixelDepth = 0;
    uint32_t layerCount = 0;
    uint32_t faceCount = 1;
    uint32_t levelCount = 1;
    uint32_t supercompressionScheme = 0;

    uint32_t dfdByteOffset = 0;
    uint32_t dfdByteLength = 0;
    uint32_t kvdByteOffset = 0;
    uint32_t kvdByteLength = 0;
    uint64_t sgdByteOffset = 0;
    uint64_t sgdByteLength = 0;
};

struct KTX2LevelIndex {
    uint64_t byteOffset;
    uint64_t byteLength;
    uint64_t uncompressedByteLength;
};
#pragma pack(pop)

// API Caveman KTX2
bool ktx2_bake_hdr_to_file(const std::string& outPath, int width, int height, const float* pixels);
float* ktx2_load_from_file(const std::string& inPath, int* outWidth, int* outHeight);

#endif // ASSET_KTX_H
