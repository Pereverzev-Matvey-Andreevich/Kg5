#pragma once
#include "Types.h"
#include <array>
#include <filesystem>
#include <vector>

class Terrain {
public:
    static constexpr int LodCount = 4;
    struct Patch {
        IndexRange indices;
        int lod;
    };
    struct Selection {
        std::vector<Patch> patches;
        std::array<int, LodCount> drawn{};
        int culled = 0;
        uint32_t triangles = 0;
    };

    void Load(const std::filesystem::path& tileDirectory, std::vector<Vertex>& vertices,
              std::vector<uint32_t>& indices);
    void Select(const XMFLOAT3& camera, const Frustum* frustum, Selection& selection) const;

private:
    struct Node {
        AABB bounds;
        Patch patch;
        size_t firstChild = 0;
    };

    void BuildNode(size_t index, int x, int z, int lod, const std::vector<float>& heights,
                   std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);
    void SelectNode(size_t index, const XMFLOAT3& camera, const Frustum* frustum,
                    Selection& selection) const;

    std::vector<Node> nodes_;
};
