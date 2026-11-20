#include "TextureLoader.h"
#include <wrl/client.h>
#include <limits>
using Microsoft::WRL::ComPtr;

namespace {
struct ComApartment {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~ComApartment() { if (SUCCEEDED(result)) CoUninitialize(); }
};
}

TextureData LoadTextureWIC(const std::wstring& path)
{
    TextureData td;

    
    const ComApartment apartment;
    if (FAILED(apartment.result) && apartment.result != RPC_E_CHANGED_MODE)
        return td;

    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(
        CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
        return td;

    
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(
        path.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnLoad, &decoder)))
        return td;

    
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame)))
        return td;

    
    ComPtr<IWICFormatConverter> conv;
    if (FAILED(factory->CreateFormatConverter(&conv)))
        return td;
    if (FAILED(conv->Initialize(
        frame.Get(),
        GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone, nullptr, 0.0,
        WICBitmapPaletteTypeCustom)))
        return td;

    UINT w = 0, h = 0;
    if (FAILED(conv->GetSize(&w, &h)) || w == 0 || h == 0 ||
        w > (std::numeric_limits<UINT>::max)() / 4 / h)
        return td;
    td.width  = w;
    td.height = h;
    td.pixels.resize(w * h * 4);

    td.valid = SUCCEEDED(conv->CopyPixels(nullptr, w * 4,
        static_cast<UINT>(td.pixels.size()), td.pixels.data()));
    return td;
}


TextureData CreateCheckerboard(UINT size, UINT tileSize)
{
    TextureData td;
    td.width  = size;
    td.height = size;
    td.pixels.resize(size * size * 4);
    td.valid  = true;

    for (UINT y = 0; y < size; ++y)
    {
        for (UINT x = 0; x < size; ++x)
        {
            bool light = ((x / tileSize) + (y / tileSize)) % 2 == 0;
            UINT i = (y * size + x) * 4;
            td.pixels[i + 0] = light ? 210 : 50;   
            td.pixels[i + 1] = light ? 210 : 50;   
            td.pixels[i + 2] = light ? 220 : 180;  
            td.pixels[i + 3] = 255;                 
        }
    }
    return td;
}
