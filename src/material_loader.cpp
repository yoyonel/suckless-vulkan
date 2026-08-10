#include "material_loader.h"
#include <cjson/cJSON.h>
#include <cmath>

#include <fstream>
#include <iostream>
#include <sstream>

ResourceResult MaterialLoader::load_materials(const std::string& filepath, std::vector<MaterialGpu>& out_materials) {
    out_materials.clear();

    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Failed to open material file: " << filepath << '\n';
        return ResourceResult::ErrorParseFailed;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string jsonStr = buffer.str();

    cJSON* root = cJSON_Parse(jsonStr.c_str());
    if (!root) {
        std::cerr << "Failed to parse JSON: " << filepath << '\n';
        const char* error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != nullptr) {
            std::cerr << "Error before: " << error_ptr << '\n';
        }
        return ResourceResult::ErrorParseFailed;
    }

    if (!cJSON_IsArray(root)) {
        std::cerr << "Root is not a JSON array in " << filepath << '\n';
        cJSON_Delete(root);
        return ResourceResult::ErrorParseFailed;
    }

    const int numMaterials = cJSON_GetArraySize(root);
    out_materials.reserve(static_cast<size_t>(numMaterials));

    for (int i = 0; i < numMaterials; i++) {
        cJSON* item = cJSON_GetArrayItem(root, i);
        if (!cJSON_IsObject(item)) {
            continue;
        }

        // Packed as two vec4 blocks to avoid std140/std430 struct ambiguity.
        MaterialGpu mat = {};
        mat.albedo_metallic[0] = 0.0f;
        mat.albedo_metallic[1] = 0.0f;
        mat.albedo_metallic[2] = 0.0f;
        mat.albedo_metallic[3] = 0.0f;
        mat.roughness_ao_pad[0] = 0.5f;
        mat.roughness_ao_pad[1] = 1.0f;
        mat.roughness_ao_pad[2] = 0.0f;
        mat.roughness_ao_pad[3] = 0.0f;

        cJSON* albedoNode = cJSON_GetObjectItemCaseSensitive(item, "albedo");
        if (cJSON_IsArray(albedoNode) && cJSON_GetArraySize(albedoNode) >= 3) {
            mat.albedo_metallic[0] = static_cast<float>(cJSON_GetNumberValue(cJSON_GetArrayItem(albedoNode, 0)));
            mat.albedo_metallic[1] = static_cast<float>(cJSON_GetNumberValue(cJSON_GetArrayItem(albedoNode, 1)));
            mat.albedo_metallic[2] = static_cast<float>(cJSON_GetNumberValue(cJSON_GetArrayItem(albedoNode, 2)));
        }

        cJSON* metallicNode = cJSON_GetObjectItemCaseSensitive(item, "metallic");
        if (cJSON_IsNumber(metallicNode)) {
            mat.albedo_metallic[3] = static_cast<float>(cJSON_GetNumberValue(metallicNode));
        }

        cJSON* roughnessNode = cJSON_GetObjectItemCaseSensitive(item, "roughness");
        if (cJSON_IsNumber(roughnessNode)) {
            mat.roughness_ao_pad[0] = static_cast<float>(cJSON_GetNumberValue(roughnessNode));
        }

        out_materials.push_back(mat);
    }

    cJSON_Delete(root);
    return ResourceResult::Success;
}
