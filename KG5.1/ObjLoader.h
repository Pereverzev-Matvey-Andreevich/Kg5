#pragma once
#include <Windows.h>       
#include <vector>
#include <string>
#include "Types.h"


struct ObjMaterialBatch
{
    IndexRange indices;
    std::wstring albedoPath;
    std::wstring normalPath;
    XMFLOAT4 color{1, 1, 1, 1};
};

struct ObjResult
{
    std::vector<Vertex> vertices;    
    std::vector<UINT>   indices;     
    std::vector<ObjMaterialBatch> batches;
    bool                valid;

    ObjResult() : valid(false) {}
};


ObjResult LoadObj(const std::wstring& path);
