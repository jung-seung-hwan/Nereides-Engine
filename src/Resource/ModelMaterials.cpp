#include "Resource/ModelMaterials.h"
#include "Core/Log.h"
#include "Renderer/EmbeddedTextures.h"
#include <DirectXTex.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <stdexcept>

namespace nereides
{
namespace
{
using Json = nlohmann::json;

std::filesystem::path FindSettings(const std::filesystem::path& model)
{
    auto specific = model;
    specific.replace_extension(".materials.json");
    if (std::filesystem::is_regular_file(specific))
        return specific;

    auto folder = model.parent_path();
    // 기존 에셋의 model/ 또는 models/ 바로 위까지만 탐색한다.
    for (int depth = 0; depth < 2; ++depth)
    {
        for (const auto* name : {"materials.json", "material.json"})
            if (std::filesystem::is_regular_file(folder / name))
                return folder / name;
        if (folder.filename() != "model" && folder.filename() != "models")
            break;
        folder = folder.parent_path();
    }
    const auto import = model.parent_path() / "engine_import.json";
    if (std::filesystem::is_regular_file(import))
    {
        std::ifstream stream(import);
        const auto settings = Json::parse(stream);
        if (settings.value("model", "") == model.filename().string())
            return import;
    }
    return {};
}

float Number(const Json& value, float minimum, float maximum)
{
    if (!value.is_number())
        throw std::runtime_error("Material value must be a number");
    const float result = value.get<float>();
    if (!std::isfinite(result) || result < minimum || result > maximum)
        throw std::runtime_error("Material value is outside its allowed range");
    return result;
}

std::string Texture(const Json& value, const std::filesystem::path& settings)
{
    const auto text = value.get<std::string>();
    if (text.empty())
        return {}; // 명시적으로 텍스처 연결을 해제할 수 있다.
    const std::u8string utf8(text.begin(), text.end());
    const auto path = std::filesystem::weakly_canonical(settings.parent_path() /
                                                       std::filesystem::path(utf8));
    if (!std::filesystem::is_regular_file(path))
        throw std::runtime_error("Material texture missing: " + path.string());
    return path.string();
}

// 분리된 데이터 맵은 색공간 변환 없이 R 채널을 읽어 ORM으로 합친다.
// GPU 자원은 만들지 않고 PNG 바이트를 기존 텍스처 로딩 경로로 전달한다.
std::string PackOrm(const std::string& metallic, const std::string& roughness)
{
    struct ComScope
    {
        HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        ~ComScope() { if (SUCCEEDED(result)) CoUninitialize(); }
    } com;
    if (FAILED(com.result) && com.result != RPC_E_CHANGED_MODE)
        throw std::runtime_error("Cannot initialize COM for material images");

    auto load = [](const std::string& path, DirectX::ScratchImage& output) {
        DirectX::ScratchImage source;
        if (FAILED(DirectX::LoadFromWICFile(std::filesystem::path(path).c_str(),
                                          DirectX::WIC_FLAGS_IGNORE_SRGB, nullptr, source)))
            throw std::runtime_error("Cannot decode material data map: " + path);
        if (source.GetMetadata().format == DXGI_FORMAT_R8G8B8A8_UNORM)
        {
            output = std::move(source);
            return;
        }
        if (FAILED(DirectX::Convert(*source.GetImage(0, 0, 0), DXGI_FORMAT_R8G8B8A8_UNORM,
                                   DirectX::TEX_FILTER_DEFAULT, 0, output)))
            throw std::runtime_error("Cannot convert material data map: " + path);
    };
    DirectX::ScratchImage metal, rough, packed;
    load(metallic, metal);
    load(roughness, rough);
    const auto& m = *metal.GetImage(0, 0, 0);
    const auto& r = *rough.GetImage(0, 0, 0);
    if (m.width != r.width || m.height != r.height)
        throw std::runtime_error("Metallic and roughness maps must have the same dimensions");
    if (FAILED(packed.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, m.width, m.height, 1, 1)))
        throw std::runtime_error("Cannot allocate ORM image");
    const auto& image = *packed.GetImage(0, 0, 0);
    for (size_t y = 0; y < m.height; ++y)
        for (size_t x = 0; x < m.width; ++x)
        {
            auto* pixel = image.pixels + y * image.rowPitch + x * 4;
            pixel[0] = 255; // AO가 제공되지 않으면 차폐 없음.
            pixel[1] = r.pixels[y * r.rowPitch + x * 4];
            pixel[2] = m.pixels[y * m.rowPitch + x * 4];
            pixel[3] = 255;
        }
    DirectX::Blob png;
    if (FAILED(DirectX::SaveToWICMemory(image, DirectX::WIC_FLAGS_NONE,
                                      DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), png)))
        throw std::runtime_error("Cannot encode ORM image");
    const auto* bytes = static_cast<const uint8_t*>(png.GetBufferPointer());
    const auto id = "generated:orm:" + std::to_string(metallic.size()) + ":" + metallic + roughness;
    EmbeddedTextures::Register(id, std::make_shared<const std::vector<uint8_t>>(
                                       bytes, bytes + png.GetBufferSize()));
    return id;
}

void Apply(const Json& entry, const std::filesystem::path& path, MaterialData& material)
{
    if (!entry.is_object())
        throw std::runtime_error("Material entry must be an object");
    if (entry.contains("basecolor"))
        material.baseColorTexture = Texture(entry.at("basecolor"), path);
    if (entry.contains("normal"))
        material.normalTexture = Texture(entry.at("normal"), path);
    // OpenGL/DirectX 선택은 이름만 보고 추측하지 않는다. normal이 없으면 FBX 연결을 유지한다.
    if (entry.contains("orm"))
    {
        material.metallicRoughnessTexture = Texture(entry.at("orm"), path);
        material.occlusionTexture = material.metallicRoughnessTexture;
        if (!material.metallicRoughnessTexture.empty())
            material.metallic = material.roughness = 1;
    }
    if (entry.contains("metallic_texture") || entry.contains("roughness_texture"))
    {
        if (entry.contains("orm"))
            throw std::runtime_error("Specify ORM or separate maps, not both");
        material.metallicRoughnessTexture = PackOrm(Texture(entry.at("metallic_texture"), path),
                                                   Texture(entry.at("roughness_texture"), path));
        material.occlusionTexture = material.metallicRoughnessTexture;
        material.metallic = material.roughness = 1;
    }
    if (entry.contains("metallic"))
        material.metallic = Number(entry.at("metallic"), 0, 1);
    if (entry.contains("roughness"))
        material.roughness = Number(entry.at("roughness"), 0, 1);
    if (entry.contains("basecolor_factor"))
    {
        const auto& c = entry.at("basecolor_factor");
        if (!c.is_array() || c.size() != 4)
            throw std::runtime_error("basecolor_factor requires four values");
        material.baseColor = {Number(c[0], 0, 1), Number(c[1], 0, 1),
                              Number(c[2], 0, 1), Number(c[3], 0, 1)};
    }
    if (entry.contains("emissive_texture"))
        material.emissiveTexture = Texture(entry.at("emissive_texture"), path);
    if (entry.contains("emission") || entry.contains("emission_strength"))
    {
        const float strength = Number(entry.at(entry.contains("emission_strength") ?
                                               "emission_strength" : "emission"), 0, 65504);
        // 색 텍스처가 있으면 그 색, 없으면 기본 색을 발광 색으로 사용한다.
        const auto color = material.emissiveTexture.empty() ? material.baseColor :
                                                            DirectX::XMFLOAT4{1, 1, 1, 1};
        material.emissive = {color.x * strength, color.y * strength, color.z * strength};
    }
}
}

void ApplyModelMaterials(const std::filesystem::path& modelPath,
                         std::vector<MaterialData>& materials)
{
    const auto path = FindSettings(modelPath);
    if (path.empty())
        return;
    try
    {
        std::ifstream stream(path);
        const auto root = Json::parse(stream);
        std::vector<Json> entries;
        if (path.filename() == "engine_import.json")
        {
            if (materials.size() != 1)
                throw std::runtime_error("engine_import textures require exactly one material");
            const auto& textures = root.at("textures");
            Json entry = {{"material", materials.front().name}};
            for (const auto& [source, target] : std::vector<std::pair<std::string, std::string>>{
                     {"base_color", "basecolor"}, {"normal", "normal"},
                     {"metallic", "metallic_texture"}, {"roughness", "roughness_texture"},
                     {"emission", "emissive_texture"}})
            {
                if (!textures.contains(source))
                    continue;
                const auto& texture = textures.at(source);
                if ((source == "metallic" || source == "roughness") &&
                    (texture.value("channel", "R") != "R" || texture.value("srgb", false)))
                    throw std::runtime_error("Separate data maps require linear R channel");
                entry[target] = texture.at("file");
            }
            // preview_strength는 콘텐츠 미리보기 값이므로 런타임 계수로 강제하지 않는다.
            entries.push_back(std::move(entry));
        }
        else if (root.contains("material"))
            entries.push_back(root);
        else
        {
            if (!root.is_object())
                throw std::runtime_error("Materials settings must be an object");
            for (const auto& entry : root)
                entries.push_back(entry);
        }
        auto result = materials;
        std::set<std::string> names;
        for (const auto& entry : entries)
        {
            const auto name = entry.at("material").get<std::string>();
            if (!names.insert(name).second)
                throw std::runtime_error("Duplicate material setting: " + name);
            bool matched = false;
            for (auto& material : result)
                if (material.name == name)
                {
                    Apply(entry, path, material);
                    matched = true;
                }
            if (!matched)
                Log::Write(LogLevel::Warning, "Material name not found: " + name + " in " + modelPath.string());
        }
        materials = std::move(result);
        Log::Write(LogLevel::Info, "Applied material settings: " + path.string());
    }
    catch (const std::exception& error)
    {
        throw std::runtime_error("Material settings " + path.string() + ": " + error.what());
    }
}
}
