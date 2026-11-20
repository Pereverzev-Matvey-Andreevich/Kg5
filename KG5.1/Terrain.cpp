#include "Terrain.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace {
constexpr int PatchCells = 32;
constexpr int PatchSamples = PatchCells + 1;
constexpr int TilesPerSide = 1 << (Terrain::LodCount - 1);
constexpr int Cells = TilesPerSide * PatchCells;
constexpr float WorldSize = 512.0f;
constexpr float SampleSpacing = WorldSize / Cells;
constexpr float HeightRange = 190.0f;
constexpr float SkirtBottom = -1.0f;

float WorldCoordinate(int sample)
{
    return sample * SampleSpacing - WorldSize / 2;
}

float Sample(const std::vector<float>& heights, int x, int z)
{
    return heights[z * (Cells + 1) + x];
}

void ReadTile(const std::filesystem::path& path, int tileX, int tileZ, std::vector<float>& heights)
{
    std::ifstream file(path, std::ios::binary);
    std::string magic;
    int width = 0, height = 0, maxValue = 0;
    if (!(file >> magic >> width >> height >> maxValue) || magic != "P5" ||
        width != PatchSamples || height != PatchSamples || maxValue != 65535) {
        throw std::runtime_error("Missing or invalid heightmap tile: " + path.filename().string());
    }
    file.ignore((std::numeric_limits<std::streamsize>::max)(), '\n');
    for (int z = 0; z < PatchSamples; ++z) {
        for (int x = 0; x < PatchSamples; ++x) {
            unsigned char pixel[2];
            if (!file.read(reinterpret_cast<char*>(pixel), sizeof(pixel)))
                throw std::runtime_error("Truncated heightmap tile: " + path.filename().string());
            const int sample = (pixel[0] << 8) | pixel[1];
            const int targetX = tileX * PatchCells + x;
            const int targetZ = tileZ * PatchCells + z;
            heights[targetZ * (Cells + 1) + targetX] = sample * (HeightRange / 65535.0f);
        }
    }
}

Vertex MakeVertex(const std::vector<float>& heights, int x, int z, int sampleStep)
{
    const int left = (std::max)(0, x - sampleStep);
    const int right = (std::min)(Cells, x + sampleStep);
    const int down = (std::max)(0, z - sampleStep);
    const int up = (std::min)(Cells, z + sampleStep);
    const float dx = (Sample(heights, right, z) - Sample(heights, left, z)) / ((right - left) * SampleSpacing);
    const float dz = (Sample(heights, x, up) - Sample(heights, x, down)) / ((up - down) * SampleSpacing);
    const float h = Sample(heights, x, z);
    XMFLOAT3 normal;
    XMStoreFloat3(&normal, XMVector3Normalize(XMVectorSet(-dx, 1, -dz, 0)));

    const float rock = std::clamp((1 - normal.y) * 3.2f + (h - 65) / 75, 0.0f, 1.0f);
    const float gravel = std::clamp((14 - h) / 12, 0.0f, 1.0f);
    const XMFLOAT3 grass{0.095f, 0.24f, 0.085f};
    const XMFLOAT3 riverbed{0.32f, 0.24f, 0.13f};
    const XMFLOAT3 stone{0.43f, 0.32f, 0.22f};
    const XMFLOAT4 color{
        std::lerp(std::lerp(grass.x, riverbed.x, gravel), stone.x, rock),
        std::lerp(std::lerp(grass.y, riverbed.y, gravel), stone.y, rock),
        std::lerp(std::lerp(grass.z, riverbed.z, gravel), stone.z, rock), 1
    };
    return { { WorldCoordinate(x), h, WorldCoordinate(z) }, normal, color, {} };
}

void AddSkirt(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
              uint32_t first, uint32_t step, uint32_t edgeCells)
{
    const auto bottom = static_cast<uint32_t>(vertices.size());
    for (uint32_t i = 0; i <= edgeCells; ++i) {
        Vertex v = vertices[first + i * step];
        v.Position.y = SkirtBottom;
        vertices.push_back(v);
    }
    for (uint32_t i = 0; i < edgeCells; ++i) {
        const auto a = first + i * step;
        const auto b = a + step;
        const auto c = bottom + i;
        const auto d = c + 1;
        // Draw both sides so a skirt also seals a join seen from the neighbouring patch.
        indices.insert(indices.end(), { a, b, c, b, d, c, c, b, a, c, d, b });
    }
}
}

void Terrain::Load(const std::filesystem::path& tileDirectory, std::vector<Vertex>& vertices,
                   std::vector<uint32_t>& indices)
{
    std::vector<float> heights((Cells + 1) * (Cells + 1));
    for (int z = 0; z < TilesPerSide; ++z) {
        for (int x = 0; x < TilesPerSide; ++x) {
            const auto file = "height_" + std::to_string(x) + "_" + std::to_string(z) + ".pgm";
            ReadTile(tileDirectory / file, x, z, heights);
        }
    }
    nodes_.assign(1, {});
    vertices.clear();
    indices.clear();
    BuildNode(0, 0, 0, LodCount - 1, heights, vertices, indices);
}

void Terrain::BuildNode(size_t index, int x, int z, int lod, const std::vector<float>& heights,
                        std::vector<Vertex>& vertices, std::vector<uint32_t>& indices)
{
    const int cells = PatchCells << lod;
    const int meshCells = PatchCells >> lod;
    const int meshSamples = meshCells + 1;
    // Parent nodes cover more ground while drawing fewer cells.
    const int sampleStep = cells / meshCells;
    const auto start = static_cast<uint32_t>(vertices.size());
    const auto first = static_cast<uint32_t>(indices.size());
    float maxHeight = 0;
    for (int row = 0; row < meshSamples; ++row) {
        for (int col = 0; col < meshSamples; ++col) {
            auto vertex = MakeVertex(heights, x + col * sampleStep, z + row * sampleStep, sampleStep);
            vertex.TexCoord = { static_cast<float>(col) / meshCells,
                                static_cast<float>(row) / meshCells };
            maxHeight = (std::max)(maxHeight, vertex.Position.y);
            vertices.push_back(vertex);
        }
    }
    for (int row = 0; row < meshCells; ++row) {
        for (int col = 0; col < meshCells; ++col) {
            const auto a = start + row * meshSamples + col;
            const auto b = a + 1;
            const auto c = a + meshSamples;
            const auto d = c + 1;
            indices.insert(indices.end(), { a, c, b, b, c, d });
        }
    }
    AddSkirt(vertices, indices, start, 1, meshCells);
    AddSkirt(vertices, indices, start + meshCells * meshSamples, 1, meshCells);
    AddSkirt(vertices, indices, start, meshSamples, meshCells);
    AddSkirt(vertices, indices, start + meshCells, meshSamples, meshCells);

    nodes_[index].patch = { { first, static_cast<uint32_t>(indices.size()) - first }, lod };
    if (lod > 0) {
        const size_t firstChild = nodes_.size();
        nodes_[index].firstChild = firstChild;
        nodes_.resize(firstChild + 4);
        const int half = cells / 2;
        for (size_t child = 0; child < 4; ++child) {
            const int childX = x + (child % 2 ? half : 0);
            const int childZ = z + (child / 2 ? half : 0);
            BuildNode(firstChild + child, childX, childZ, lod - 1, heights, vertices, indices);
            maxHeight = (std::max)(maxHeight, nodes_[firstChild + child].bounds.max.y);
        }
    }
    nodes_[index].bounds = { { WorldCoordinate(x), SkirtBottom, WorldCoordinate(z) },
                             { WorldCoordinate(x + cells), maxHeight, WorldCoordinate(z + cells) } };
}

void Terrain::Select(const XMFLOAT3& camera, const Frustum* frustum, Selection& selection) const
{
    selection.patches.clear();
    selection.drawn.fill(0);
    selection.culled = 0;
    selection.triangles = 0;
    if (!nodes_.empty())
        SelectNode(0, camera, frustum, selection);
}

void Terrain::SelectNode(size_t index, const XMFLOAT3& camera, const Frustum* frustum,
                         Selection& selection) const
{
    const auto& node = nodes_[index];
    if (frustum && !IntersectsFrustum(node.bounds, *frustum)) {
        ++selection.culled;
        return;
    }
    const auto& a = node.bounds.min;
    const auto& b = node.bounds.max;
    const float dx = camera.x - std::clamp(camera.x, a.x, b.x);
    const float dy = camera.y - std::clamp(camera.y, 0.0f, b.y);
    const float dz = camera.z - std::clamp(camera.z, a.z, b.z);
    const float refineDistance = (b.x - a.x) * 1.15f;
    if (node.firstChild && dx * dx + dy * dy + dz * dz < refineDistance * refineDistance) {
        for (size_t child = node.firstChild; child < node.firstChild + 4; ++child)
            SelectNode(child, camera, frustum, selection);
        return;
    }
    selection.patches.push_back(node.patch);
    ++selection.drawn[node.patch.lod];
    selection.triangles += node.patch.indices.count / 3;
}
