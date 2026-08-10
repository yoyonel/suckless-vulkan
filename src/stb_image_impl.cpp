#include "tracy_client.h"
#include <cstdlib>

inline void* stbi_tracy_malloc(size_t size) {
    void* ptr = std::malloc(size);
    if (ptr)
        SVK_TRACY_ALLOC(ptr, size);
    return ptr;
}
inline void* stbi_tracy_realloc(void* ptr, size_t size) {
    if (ptr)
        SVK_TRACY_FREE(ptr);
    void* new_ptr = std::realloc(ptr, size);
    if (new_ptr)
        SVK_TRACY_ALLOC(new_ptr, size);
    return new_ptr;
}
inline void stbi_tracy_free(void* ptr) {
    if (ptr)
        SVK_TRACY_FREE(ptr);
    std::free(ptr);
}

#define STBI_MALLOC(sz) stbi_tracy_malloc(sz)
#define STBI_REALLOC(p, newsz) stbi_tracy_realloc(p, newsz)
#define STBI_FREE(p) stbi_tracy_free(p)

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
