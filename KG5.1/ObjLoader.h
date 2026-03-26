#pragma once
#include <Windows.h>       
#include <vector>
#include <string>
#include "Types.h"


struct ObjResult
{
    std::vector<Vertex> vertices;    
    std::vector<UINT>   indices;     
    std::wstring        texturePath; 
    bool                valid;

    ObjResult() : valid(false) {}
};


ObjResult LoadObj(const std::wstring& path);
