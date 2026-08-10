#include "result.h"
#ifndef MATERIAL_LOADER_H
#define MATERIAL_LOADER_H

#include <string>
#include <vector>

struct MaterialGpu {
    float albedo_metallic[4];
    float roughness_ao_pad[4];
};

class MaterialLoader {
  public:
    static ResourceResult load_materials(const std::string& filepath, std::vector<MaterialGpu>& out_materials);
};

#endif
