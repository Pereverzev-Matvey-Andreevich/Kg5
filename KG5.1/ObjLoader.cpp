#include <Windows.h>       
#include "ObjLoader.h"
#include <fstream>
#include <sstream>
#include <map>
#include <charconv>

using namespace DirectX;


struct Material
{
    XMFLOAT4     diffuse;
    std::wstring texturePath;
    std::wstring normalPath;
    Material() : diffuse(1.0f, 1.0f, 1.0f, 1.0f) {}
};

static void ParseFaceVert(const std::string& token,
                           int& p, int& t, int& n)
{
    p = t = n = 0;
    const char* start = token.data();
    const char* end = start + token.size();
    for (int* index : { &p, &t, &n }) {
        const auto parsed = std::from_chars(start, end, *index);
        if (parsed.ptr == end || *parsed.ptr != '/')
            break;
        start = parsed.ptr + 1;
    }
}

static std::map<std::string, Material>
LoadMtl(const std::wstring& mtlPath, const std::wstring& dir)
{
    std::map<std::string, Material> mats;
    std::ifstream f(mtlPath);
    if (!f) return mats;

    Material*   cur = NULL;
    std::string line;
    while (std::getline(f, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        std::istringstream ss(line);
        std::string token;
        ss >> token;

        if (token == "newmtl")
        {
            std::string name; ss >> name;
            mats[name] = Material();
            cur = &mats[name];
        }
        else if (cur && token == "Kd")
        {
            float r, g, b; ss >> r >> g >> b;
            cur->diffuse = XMFLOAT4(r, g, b, 1.0f);
        }
        else if (cur && (token == "map_Kd" || token == "norm"))
        {
            std::string texName; ss >> texName;
            auto& dest = token == "map_Kd" ? cur->texturePath : cur->normalPath;
            dest = dir + std::wstring(texName.begin(), texName.end());
        }
    }
    return mats;
}

ObjResult LoadObj(const std::wstring& path)
{
    ObjResult out;
    std::ifstream f(path);
    if (!f) return out;

    std::wstring dir;
    {
        size_t sep = path.find_last_of(L"\\/");
        if (sep != std::wstring::npos) dir = path.substr(0, sep + 1);
    }

    std::vector<XMFLOAT3> positions;
    std::vector<XMFLOAT3> normals;
    std::vector<XMFLOAT2> texcoords;

    std::map<std::string, Material> materials;
    struct FaceGroup { Material material; std::vector<UINT> indices; };
    std::vector<FaceGroup> groups(1);
    std::map<std::string, size_t> groupByMaterial;
    size_t activeGroup = 0;
    std::vector<int> pIdx, tIdx, nIdx;

    std::string line;
    while (std::getline(f, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        std::istringstream ss(line);
        std::string token;
        ss >> token;

        if (token == "v")
        {
            float x, y, z; ss >> x >> y >> z;
            positions.push_back(XMFLOAT3(x, y, z));
        }

        else if (token == "vt")
        {
            float u, v; ss >> u >> v;
            texcoords.push_back(XMFLOAT2(u, 1.0f - v)); 
        }
        
        else if (token == "vn")
        {
            float x, y, z; ss >> x >> y >> z;
            normals.push_back(XMFLOAT3(x, y, z));
        }
        
        else if (token == "mtllib")
        {
            std::string name; ss >> name;
            std::wstring mtlPath = dir + std::wstring(name.begin(), name.end());
            materials = LoadMtl(mtlPath, dir);
        }
        
        else if (token == "usemtl")
        {
            std::string name; ss >> name;
            const auto material = materials.find(name);
            activeGroup = 0;
            if (material == materials.end())
                continue;
            const auto [group, added] = groupByMaterial.try_emplace(name, groups.size());
            if (added)
                groups.push_back({ material->second, {} });
            activeGroup = group->second;
        }
        
        else if (token == "f")
        {
            auto& group = groups[activeGroup];
            pIdx.clear();
            tIdx.clear();
            nIdx.clear();
            std::string vert;
            while (ss >> vert)
            {
                int p = 0, t = 0, n = 0;
                ParseFaceVert(vert, p, t, n);

                
                if (p < 0) p = (int)positions.size() + p + 1;
                if (t < 0) t = (int)texcoords.size() + t + 1;
                if (n < 0) n = (int)normals.size()   + n + 1;

                pIdx.push_back(p);
                tIdx.push_back(t);
                nIdx.push_back(n);
            }

            
            for (int i = 1; i + 1 < (int)pIdx.size(); ++i)
            {
                
                int fan[3] = { 0, i, i + 1 };
                for (int fi = 0; fi < 3; ++fi)
                {
                    int j = fan[fi];
                    Vertex v;
                    v.Position = XMFLOAT3(0, 0, 0);
                    v.Normal   = XMFLOAT3(0, 1, 0);
                    v.Color    = group.material.diffuse;
                    v.TexCoord = XMFLOAT2(0, 0);

                    int pi = pIdx[j];
                    if (pi > 0 && pi <= (int)positions.size())
                        v.Position = positions[pi - 1];

                    int ni = nIdx[j];
                    if (ni > 0 && ni <= (int)normals.size())
                        v.Normal = normals[ni - 1];

                    int ti = tIdx[j];
                    if (ti > 0 && ti <= (int)texcoords.size()) {
                        v.TexCoord = texcoords[ti - 1];
                        if (!group.material.texturePath.empty())
                            v.TexCoord.y = 1 - v.TexCoord.y;
                    }

                    group.indices.push_back((UINT)out.vertices.size());
                    out.vertices.push_back(v);
                }
            }
        }
    }

    if (out.vertices.empty()) return out;

    out.indices.reserve(out.vertices.size());
    for (const auto& group : groups) {
        if (group.indices.empty())
            continue;
        const auto& material = group.material;
        out.batches.push_back({ { static_cast<UINT>(out.indices.size()), static_cast<UINT>(group.indices.size()) },
                                material.texturePath, material.normalPath, material.diffuse });
        out.indices.insert(out.indices.end(), group.indices.begin(), group.indices.end());
    }

    
    if (normals.empty())
    {
        for (UINT i = 0; i + 2 < (UINT)out.indices.size(); i += 3)
        {
            Vertex& v0 = out.vertices[out.indices[i]];
            Vertex& v1 = out.vertices[out.indices[i + 1]];
            Vertex& v2 = out.vertices[out.indices[i + 2]];

            XMVECTOR p0 = XMLoadFloat3(&v0.Position);
            XMVECTOR p1 = XMLoadFloat3(&v1.Position);
            XMVECTOR p2 = XMLoadFloat3(&v2.Position);
            XMVECTOR fn = XMVector3Normalize(
                XMVector3Cross(p1 - p0, p2 - p0));
            XMFLOAT3 nf;
            XMStoreFloat3(&nf, fn);
            v0.Normal = nf;
            v1.Normal = nf;
            v2.Normal = nf;
        }
    }

    out.valid = true;
    return out;
}
