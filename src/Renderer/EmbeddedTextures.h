#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace nereides
{
// 모델 파일 안에 들어 있는 압축 이미지(PNG/JPG 등)의 보관소.
// 모델 로더가 "<모델 파일명>/<번호>" 이름으로 등록하고, 렌더러의 텍스처 캐시가 같은 이름으로 찾아 디코딩함.
// 재질은 파일 경로와 똑같이 이름만 들고 있으면 되므로 Scene/Resource가 Device를 몰라도 됨.
class EmbeddedTextures final
{
public:
    using Bytes = std::shared_ptr<const std::vector<std::uint8_t>>;

    static void Register(const std::string& name, Bytes bytes)
    {
        Store()[name] = std::move(bytes);
    }

    static Bytes Find(const std::string& name)
    {
        const auto found = Store().find(name);
        return found == Store().end() ? nullptr : found->second;
    }

private:
    static std::unordered_map<std::string, Bytes>& Store()
    {
        static std::unordered_map<std::string, Bytes> store;
        return store;
    }
};
} // namespace nereides
