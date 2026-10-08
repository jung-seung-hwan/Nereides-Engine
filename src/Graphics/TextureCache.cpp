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


// 현재 스레드의 COM 사용을 준비하고, 성공한 초기화에 대응해 종료 처리
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

    // 경로를 정규화하고, 이미 준비된 텍스처가 있으면 재사용
    auto key = std::filesystem::weakly_canonical(path);
    if (auto found = m_textures.find(key); found != m_textures.end())
        return found->second.Get();

    Apartment apartment;
    using Microsoft::WRL::ComPtr;

    // WIC으로 이미지 파일을 열고 첫 번째 프레임을 읽음
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

    // 이미지를 픽셀당 4바이트 RGBA 형식으로 변환해 CPU 메모리에 보관
    ComPtr<IWICFormatConverter> converter;
    Check(factory->CreateFormatConverter(converter.GetAddressOf()));
    Check(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                                nullptr, 0, WICBitmapPaletteTypeCustom));
    std::vector<unsigned char> pixels(std::size_t(width) * height * 4);
    Check(
        converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data()));

    // 초기 픽셀 데이터로 변경 불가능한 GPU 텍스처 생성 준비
    // Mip은 한 단계이며, 셰이더에서 읽는 용도로 사용
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

    // GPU 텍스처를 셰이더에서 읽을 수 있도록 SRV 생성
    ComPtr<ID3D11ShaderResourceView> view;
    Check(m_device->CreateShaderResourceView(texture.Get(), nullptr, view.GetAddressOf()));

    // SRV의 소유 참조를 캐시에 보관하고 원시 포인터 반환
    // SRV가 텍스처를 참조하므로 지역 texture가 정리되어도 자원 유지
    return m_textures.emplace(key, std::move(view)).first->second.Get();
}
} // namespace nereides
