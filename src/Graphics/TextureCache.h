#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <filesystem>
#include <map>

namespace nereides
{
// 이미지 파일을 GPU 텍스처로 만들고, 경로별로 SRV를 캐시해 재사용
class TextureCache final
{
public:
    // 텍스처 생성에 사용할 외부 장치를 연결. 장치를 직접 생성하거나 소유하지 않음
    void SetDevice(ID3D11Device* device)
    {
        m_device = device;
    }

    // 캐시된 SRV를 반환하고, 없으면 이미지 로드와 GPU 자원 생성 후 보관
    // 반환 포인터는 빌려 쓰는 참조이며, Clear나 캐시 파괴 이후 유효성을 보장하지 않음
    ID3D11ShaderResourceView* Get(const std::filesystem::path& path);

    // 캐시가 보유한 SRV 참조를 모두 해제. 이후 요청 시 다시 로드
    void Clear()
    {
        m_textures.clear();
    }

private:
    ID3D11Device* m_device = nullptr;
    std::map<std::filesystem::path, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> m_textures;
};
} // namespace nereides
