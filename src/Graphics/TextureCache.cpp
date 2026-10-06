#include "Graphics/TextureCache.h"
#include <wincodec.h>
#include <vector>
#include <stdexcept>
namespace nereides
{
namespace
{
void Check(HRESULT result)
{
    if (FAILED(result))
        throw std::runtime_error("WIC/DX11 texture loading failed: " +
                                 std::to_string(static_cast<unsigned>(result)));
}
struct Apartment
{
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Apartment()
    {
        if (result != RPC_E_CHANGED_MODE)
            Check(result);
    }
    ~Apartment()
    {
        if (SUCCEEDED(result))
            CoUninitialize();
    }
};
} // namespace
ID3D11ShaderResourceView* TextureCache::Get(const std::filesystem::path& path)
{
    if (!m_device)
        throw std::runtime_error("Texture device is unavailable");
    auto key = std::filesystem::weakly_canonical(path);
    if (auto found = m_textures.find(key); found != m_textures.end())
        return found->second.Get();
    Apartment apartment;
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory;
    Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                           IID_PPV_ARGS(factory.GetAddressOf())));
    ComPtr<IWICBitmapDecoder> decoder;
    Check(factory->CreateDecoderFromFilename(key.c_str(), nullptr, GENERIC_READ,
                                             WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf()));
    ComPtr<IWICBitmapFrameDecode> frame;
    Check(decoder->GetFrame(0, frame.GetAddressOf()));
    UINT width = 0, height = 0;
    Check(frame->GetSize(&width, &height));
    if (!width || !height || width > 8192 || height > 8192)
        throw std::runtime_error("Texture dimensions unsupported");
    ComPtr<IWICFormatConverter> converter;
    Check(factory->CreateFormatConverter(converter.GetAddressOf()));
    Check(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                                nullptr, 0, WICBitmapPaletteTypeCustom));
    std::vector<unsigned char> pixels(std::size_t(width) * height * 4);
    Check(
        converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data()));
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = desc.ArraySize = 1;
    desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{};
    data.pSysMem = pixels.data();
    data.SysMemPitch = width * 4;
    ComPtr<ID3D11Texture2D> texture;
    Check(m_device->CreateTexture2D(&desc, &data, texture.GetAddressOf()));
    ComPtr<ID3D11ShaderResourceView> view;
    Check(m_device->CreateShaderResourceView(texture.Get(), nullptr, view.GetAddressOf()));
    return m_textures.emplace(key, std::move(view)).first->second.Get();
}
} // namespace nereides
