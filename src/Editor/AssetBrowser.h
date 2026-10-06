#pragma once
#include <filesystem>
#include <functional>
#include <map>
#include <vector>
#include <DirectXMath.h>
namespace nereides
{
struct AssetFile
{
    std::filesystem::path path;
    bool directory = false;
    std::uintmax_t bytes = 0;
};
struct AssetInfo
{
    std::size_t meshes = 0, clips = 0;
};
class AssetBrowser final
{
public:
    void SetRoot(const std::filesystem::path&);
    void Refresh();
    void Navigate(const std::filesystem::path&);
    std::vector<AssetFile> Visible() const;
    void Remember(const std::filesystem::path&, std::size_t meshes, std::size_t clips);
    void Draw(bool playing, const std::function<std::filesystem::path()>& browse,
              const std::function<void(const std::filesystem::path&)>& place);

private:
    friend struct EditorTestAccess;
    std::filesystem::path m_root, m_folder, m_selected;
    std::vector<AssetFile> m_index;
    std::vector<std::filesystem::path> m_back;
    std::map<std::filesystem::path, AssetInfo> m_known;
    char m_search[128]{};
    int m_type = 0;
    bool m_ascending = true;
    std::string m_scanStatus;
    std::map<std::filesystem::path, DirectX::XMFLOAT2> m_rowCenters;
    DirectX::XMFLOAT2 m_placeButton{}, m_searchBox{}, m_backButton{}, m_upButton{};
};
} // namespace nereides
