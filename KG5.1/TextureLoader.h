#pragma once
#include <Windows.h>
#include <wincodec.h>
#include <vector>
#include <string>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")


struct TextureData
{
    std::vector<uint8_t> pixels; 
    UINT width  = 0;
    UINT height = 0;
    bool valid  = false;
};


TextureData LoadTextureWIC(const std::wstring& path);


TextureData CreateCheckerboard(UINT size = 256, UINT tileSize = 32);
