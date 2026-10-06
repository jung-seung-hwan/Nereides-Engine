#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <filesystem>
#include <map>
namespace nereides
{
class TextureCache final
{
public:
    void SetDevice(ID3D11Device* device){m_device=device;}
    ID3D11ShaderResourceView* Get(const std::filesystem::path& path);
    void Clear(){m_textures.clear();}
private:
    ID3D11Device* m_device=nullptr;
    std::map<std::filesystem::path,Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> m_textures;
};
}
