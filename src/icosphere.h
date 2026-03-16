#ifndef ICOSPHERE_H
#define ICOSPHERE_H
#include "vk_engine.h"
#include <algorithm>
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>

class Icosphere {
  public:
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    void generate(int subdivisions) {
        const float X = 0.525731112119133606f;
        const float Z = 0.850650808352039932f;
        const float N = 0.0f;

        std::vector<glm::vec3> base_pos = {{-X, N, Z}, {X, N, Z},   {-X, N, -Z}, {X, N, -Z}, {N, Z, X},  {N, Z, -X},
                                           {N, -Z, X}, {N, -Z, -X}, {Z, X, N},   {-Z, X, N}, {Z, -X, N}, {-Z, -X, N}};

        indices = {0, 4,  1, 0, 9, 4,  9, 5,  4, 4,  5, 8, 4, 8, 1, 8, 10, 1,  8, 3, 10, 5, 3,  8, 5, 2, 3, 2, 7, 3,
                   7, 10, 3, 7, 6, 10, 7, 11, 6, 11, 0, 6, 0, 1, 6, 6, 1,  10, 9, 0, 11, 9, 11, 2, 9, 2, 5, 7, 2, 11};

        for (const auto& p : base_pos) {
            glm::vec3 norm = glm::normalize(p);
            glm::vec3 col = norm * 0.5f + 0.5f; // Normale convertie en couleur RGB
            vertices.push_back({{norm.x, norm.y, norm.z}, {col.x, col.y, col.z}});
        }

        for (int i = 0; i < subdivisions; ++i) {
            std::vector<uint32_t> new_indices;
            std::unordered_map<uint64_t, uint32_t> edge_map;

            for (size_t j = 0; j < indices.size(); j += 3) {
                uint32_t v0 = indices[j];
                uint32_t v1 = indices[j + 1];
                uint32_t v2 = indices[j + 2];

                uint32_t a = get_midpoint(v0, v1, edge_map);
                uint32_t b = get_midpoint(v1, v2, edge_map);
                uint32_t c = get_midpoint(v2, v0, edge_map);

                new_indices.insert(new_indices.end(), {v0, a, c, v1, b, a, v2, c, b, a, b, c});
            }
            indices = std::move(new_indices);
        }
    }

  private:
    uint32_t get_midpoint(uint32_t p1, uint32_t p2, std::unordered_map<uint64_t, uint32_t>& edge_map) {
        uint32_t min_p = std::min(p1, p2);
        uint32_t max_p = std::max(p1, p2);
        uint64_t key = ((uint64_t)min_p << 32) | max_p;

        if (edge_map.find(key) != edge_map.end())
            return edge_map[key];

        glm::vec3 pos1 = glm::vec3(vertices[p1].position[0], vertices[p1].position[1], vertices[p1].position[2]);
        glm::vec3 pos2 = glm::vec3(vertices[p2].position[0], vertices[p2].position[1], vertices[p2].position[2]);

        glm::vec3 mid = glm::normalize((pos1 + pos2) * 0.5f);
        glm::vec3 col = mid * 0.5f + 0.5f;

        vertices.push_back({{mid.x, mid.y, mid.z}, {col.x, col.y, col.z}});
        uint32_t index = vertices.size() - 1;
        edge_map[key] = index;
        return index;
    }
};
#endif // ICOSPHERE_H
