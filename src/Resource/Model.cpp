#include "Resource/Model.h"
#include "Core/Log.h"
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/config.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <unordered_map>

namespace nereides
{
using namespace DirectX;
namespace
{
XMFLOAT4X4 Matrix(const aiMatrix4x4& m)
{
    // Assimp column vectors -> engine row vectors.
    return {m.a1, m.b1, m.c1, m.d1, m.a2, m.b2, m.c2, m.d2,
            m.a3, m.b3, m.c3, m.d3, m.a4, m.b4, m.c4, m.d4};
}
XMFLOAT3 Vector(const aiVector3D& v)
{
    return {v.x, v.y, v.z};
}
XMFLOAT4 Quaternion(const aiQuaternion& q)
{
    return {q.x, q.y, q.z, q.w};
}
Pose Decompose(const aiMatrix4x4& matrix)
{
    aiVector3D scale, position;
    aiQuaternion rotation;
    matrix.Decompose(scale, rotation, position);
    Pose result{Vector(position), Vector(scale), Quaternion(rotation)};
    const auto original = Matrix(matrix);
    XMFLOAT4X4 reconstructed;
    XMStoreFloat4x4(&reconstructed, result.Matrix());
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned col = 0; col < 4; ++col)
            if (!std::isfinite(original.m[row][col]) || !std::isfinite(reconstructed.m[row][col]) ||
                std::abs(original.m[row][col] - reconstructed.m[row][col]) >
                    .0001f * std::max(1.f, std::abs(original.m[row][col])))
                throw std::runtime_error("Node contains unsupported shear or invalid transform");
    return result;
}
std::string TexturePath(const aiMaterial& material, aiTextureType type,
                        const std::filesystem::path& path)
{
    aiString name;
    if (material.GetTexture(type, 0, &name) != AI_SUCCESS)
        return {};
    if (name.length && name.C_Str()[0] == '*')
        return name.C_Str(); // Embedded data requires a later decoder.
    return (path.parent_path() / std::filesystem::path(name.C_Str())).lexically_normal().string();
}
template <class T, class Interpolate>
T Sample(const std::vector<Key<T>>& keys, double time, T fallback, Interpolate interpolate)
{
    if (keys.empty())
        return fallback;
    if (time <= keys.front().time)
        return keys.front().value;
    if (time >= keys.back().time)
        return keys.back().value;
    auto next = std::upper_bound(keys.begin(), keys.end(), time,
                                 [](double t, const auto& k) { return t < k.time; });
    const auto& a = *(next - 1);
    const auto& b = *next;
    const auto span = b.time - a.time;
    return interpolate(a.value, b.value,
                       span > 0 ? static_cast<float>((time - a.time) / span) : 1.f);
}
XMFLOAT3 Lerp(XMFLOAT3 a, XMFLOAT3 b, float t)
{
    XMFLOAT3 result;
    XMStoreFloat3(&result, XMVectorLerp(XMLoadFloat3(&a), XMLoadFloat3(&b), t));
    return result;
}
XMFLOAT4 Slerp(XMFLOAT4 a, XMFLOAT4 b, float t)
{
    XMFLOAT4 result;
    XMStoreFloat4(&result,
                  XMQuaternionNormalize(XMQuaternionSlerp(XMLoadFloat4(&a), XMLoadFloat4(&b), t)));
    return result;
}
} // namespace
XMMATRIX Pose::Matrix() const
{
    return XMMatrixScaling(scale.x, scale.y, scale.z) *
           XMMatrixRotationQuaternion(XMLoadFloat4(&rotation)) *
           XMMatrixTranslation(position.x, position.y, position.z);
}
std::shared_ptr<const ModelData> ModelCache::Load(const std::filesystem::path& path)
{
    try
    {
        const auto key = std::filesystem::weakly_canonical(path);
        if (auto found = m_cache.find(key); found != m_cache.end())
            if (auto model = found->second.lock())
                return model;
        Assimp::Importer importer;
        importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
        const auto utf8 = key.u8string();
        const aiScene* source =
            importer.ReadFile(std::string(utf8.begin(), utf8.end()),
                              aiProcess_Triangulate | aiProcess_ConvertToLeftHanded |
                                  aiProcess_LimitBoneWeights | aiProcess_GenSmoothNormals |
                                  aiProcess_ValidateDataStructure | aiProcess_GlobalScale);
        if (!source || !source->mRootNode)
            throw std::runtime_error(importer.GetErrorString());
        auto model = std::make_shared<ModelData>();
        model->path = key.string();
        std::unordered_map<std::string, unsigned> nodes;
        std::vector<const aiNode*> nodeSources;
        std::function<void(const aiNode*, int)> visit = [&](const aiNode* node, int parent) {
            const unsigned index = static_cast<unsigned>(model->nodes.size());
            if (!nodes.emplace(node->mName.C_Str(), index).second)
                throw std::runtime_error("Duplicate node name; skeleton mapping is ambiguous");
            model->nodes.push_back({node->mName.C_Str(), parent, Decompose(node->mTransformation)});
            nodeSources.push_back(node);
            for (unsigned i = 0; i < node->mNumChildren; ++i)
                visit(node->mChildren[i], static_cast<int>(index));
        };
        visit(source->mRootNode, -1);
        for (unsigned i = 0; i < source->mNumMaterials; ++i)
        {
            const auto& sourceMaterial = *source->mMaterials[i];
            MaterialData material;
            aiString name;
            sourceMaterial.Get(AI_MATKEY_NAME, name);
            material.name = name.C_Str();
            aiColor4D color(1, 1, 1, 1);
            aiGetMaterialColor(&sourceMaterial, AI_MATKEY_COLOR_DIFFUSE, &color);
            material.baseColor = {color.r, color.g, color.b, color.a};
            material.baseColorTexture = TexturePath(sourceMaterial, aiTextureType_BASE_COLOR, key);
            if (material.baseColorTexture.empty())
                material.baseColorTexture = TexturePath(sourceMaterial, aiTextureType_DIFFUSE, key);
            material.normalTexture = TexturePath(sourceMaterial, aiTextureType_NORMALS, key);
            model->materials.push_back(std::move(material));
        }
        for (unsigned node = 0; node < nodeSources.size(); ++node)
            for (unsigned slot = 0; slot < nodeSources[node]->mNumMeshes; ++slot)
            {
                const auto& sourceMesh = *source->mMeshes[nodeSources[node]->mMeshes[slot]];
                ModelPart part;
                part.node = node;
                part.material = sourceMesh.mMaterialIndex;
                auto mesh = std::make_shared<MeshData>();
                mesh->vertices.resize(sourceMesh.mNumVertices);
                for (unsigned v = 0; v < sourceMesh.mNumVertices; ++v)
                {
                    auto& vertex = mesh->vertices[v];
                    vertex.position = Vector(sourceMesh.mVertices[v]);
                    if (sourceMesh.HasNormals())
                        vertex.normal = Vector(sourceMesh.mNormals[v]);
                    if (sourceMesh.HasTextureCoords(0))
                        vertex.uv = {sourceMesh.mTextureCoords[0][v].x,
                                     sourceMesh.mTextureCoords[0][v].y};
                    if (sourceMesh.HasVertexColors(0))
                    {
                        const auto c = sourceMesh.mColors[0][v];
                        vertex.color = {c.r, c.g, c.b};
                    }
                }
                for (unsigned f = 0; f < sourceMesh.mNumFaces; ++f)
                {
                    const auto& face = sourceMesh.mFaces[f];
                    if (face.mNumIndices != 3)
                        continue;
                    mesh->indices.insert(mesh->indices.end(), face.mIndices, face.mIndices + 3);
                }
                if (mesh->vertices.empty() || mesh->indices.empty())
                    continue;
                if (sourceMesh.mNumBones >= 128)
                    throw std::runtime_error("Mesh exceeds 127 bones plus fallback binding");
                for (unsigned b = 0; b < sourceMesh.mNumBones; ++b)
                {
                    const auto& bone = *sourceMesh.mBones[b];
                    const auto target = nodes.find(bone.mName.C_Str());
                    if (target == nodes.end())
                        throw std::runtime_error("Bone has no matching node");
                    part.bones.push_back({target->second, Matrix(bone.mOffsetMatrix)});
                    for (unsigned w = 0; w < bone.mNumWeights; ++w)
                    {
                        const auto& weight = bone.mWeights[w];
                        auto& vertex = mesh->vertices.at(weight.mVertexId);
                        for (unsigned influence = 0; influence < 4; ++influence)
                            if (vertex.weights[influence] == 0)
                            {
                                vertex.bones[influence] = b;
                                vertex.weights[influence] = weight.mWeight;
                                break;
                            }
                    }
                }
                if (!part.bones.empty())
                {
                    BoneBinding fallback;
                    fallback.node = node;
                    XMStoreFloat4x4(&fallback.inverseBind, XMMatrixIdentity());
                    const auto fallbackIndex = static_cast<unsigned>(part.bones.size());
                    part.bones.push_back(fallback);
                    for (auto& vertex : mesh->vertices)
                    {
                        float sum = 0;
                        for (float w : vertex.weights)
                            sum += w;
                        if (sum > 0)
                            for (float& w : vertex.weights)
                                w /= sum;
                        else
                        {
                            vertex.bones[0] = fallbackIndex;
                            vertex.weights[0] = 1;
                        }
                    }
                }
                part.mesh = std::move(mesh);
                model->parts.push_back(std::move(part));
            }
        for (unsigned c = 0; c < source->mNumAnimations; ++c)
        {
            const auto& animation = *source->mAnimations[c];
            AnimationClip clip;
            clip.name = animation.mName.C_Str();
            const double ticks = animation.mTicksPerSecond > 0 ? animation.mTicksPerSecond : 25.;
            clip.duration = animation.mDuration / ticks;
            if (!std::isfinite(clip.duration) || clip.duration <= 0)
                throw std::runtime_error("Invalid animation duration");
            for (unsigned channel = 0; channel < animation.mNumChannels; ++channel)
            {
                const auto& sourceTrack = *animation.mChannels[channel];
                NodeTrack track;
                auto found = nodes.find(sourceTrack.mNodeName.C_Str());
                if (found == nodes.end())
                    throw std::runtime_error("Animation channel has no matching node");
                track.node = found->second;
                for (unsigned k = 0; k < sourceTrack.mNumPositionKeys; ++k)
                    track.positions.push_back({sourceTrack.mPositionKeys[k].mTime / ticks,
                                               Vector(sourceTrack.mPositionKeys[k].mValue)});
                for (unsigned k = 0; k < sourceTrack.mNumScalingKeys; ++k)
                    track.scales.push_back({sourceTrack.mScalingKeys[k].mTime / ticks,
                                            Vector(sourceTrack.mScalingKeys[k].mValue)});
                for (unsigned k = 0; k < sourceTrack.mNumRotationKeys; ++k)
                    track.rotations.push_back({sourceTrack.mRotationKeys[k].mTime / ticks,
                                               Quaternion(sourceTrack.mRotationKeys[k].mValue)});
                clip.tracks.push_back(std::move(track));
            }
            model->clips.push_back(std::move(clip));
        }
        m_cache[key] = model;
        Log::Write(LogLevel::Info, "Loaded model " + model->path +
                                       " nodes=" + std::to_string(model->nodes.size()) +
                                       " parts=" + std::to_string(model->parts.size()) +
                                       " clips=" + std::to_string(model->clips.size()));
        return model;
    }
    catch (const std::exception& error)
    {
        Log::Write(LogLevel::Error, "Model load " + path.string() + ": " + error.what());
        throw;
    }
}
void ModelCache::Prune()
{
    std::erase_if(m_cache, [](const auto& item) { return item.second.expired(); });
}
AnimationPlayer::AnimationPlayer(std::shared_ptr<const ModelData> model) : m_model(std::move(model))
{
    if (!m_model)
        throw std::invalid_argument("AnimationPlayer requires a model");
    for (const auto& node : m_model->nodes)
        m_pose.push_back(node.bind);
}
bool AnimationPlayer::Play(std::size_t clip, bool loop, double blendSeconds)
{
    if (clip >= m_model->clips.size() || !std::isfinite(blendSeconds) || blendSeconds < 0)
        return false;
    m_from = m_pose;
    m_clip = clip;
    m_loop = loop;
    m_time = 0;
    m_blendTime = 0;
    m_blendDuration = blendSeconds;
    m_playing = true;
    m_finished = false;
    Evaluate();
    return true;
}
void AnimationPlayer::Advance(double dt)
{
    if (!m_playing || !std::isfinite(dt) || dt < 0)
        return;
    const auto duration = m_model->clips[m_clip].duration;
    if (duration <= 0)
    {
        m_playing = false;
        m_finished = true;
        return;
    }
    m_time += dt;
    m_blendTime += dt;
    if (m_loop)
        m_time = std::fmod(m_time, duration);
    else if (m_time >= duration)
    {
        m_time = duration;
        m_finished = true;
    }
    Evaluate();
    if (m_finished && m_blendTime >= m_blendDuration)
        m_playing = false;
}
void AnimationPlayer::Evaluate()
{
    for (std::size_t i = 0; i < m_pose.size(); ++i)
        m_pose[i] = m_model->nodes[i].bind;
    for (const auto& track : m_model->clips[m_clip].tracks)
    {
        auto& pose = m_pose.at(track.node);
        pose.position = Sample(track.positions, m_time, pose.position, Lerp);
        pose.scale = Sample(track.scales, m_time, pose.scale, Lerp);
        pose.rotation = Sample(track.rotations, m_time, pose.rotation, Slerp);
    }
    if (m_blendDuration > 0 && m_blendTime < m_blendDuration)
    {
        const float t = static_cast<float>(m_blendTime / m_blendDuration);
        for (std::size_t i = 0; i < m_pose.size(); ++i)
        {
            m_pose[i].position = Lerp(m_from[i].position, m_pose[i].position, t);
            m_pose[i].scale = Lerp(m_from[i].scale, m_pose[i].scale, t);
            m_pose[i].rotation = Slerp(m_from[i].rotation, m_pose[i].rotation, t);
        }
    }
}
std::vector<XMFLOAT4X4> AnimationPlayer::GlobalPose() const
{
    std::vector<XMFLOAT4X4> result(m_pose.size());
    for (std::size_t i = 0; i < m_pose.size(); ++i)
    {
        auto matrix = m_pose[i].Matrix();
        const int parent = m_model->nodes[i].parent;
        if (parent >= 0)
            matrix *= XMLoadFloat4x4(&result.at(parent));
        XMStoreFloat4x4(&result[i], matrix);
    }
    return result;
}
void AppendModelDraws(const ModelComponent& component, FXMMATRIX world, RenderFrame& frame)
{
    const auto globals = component.player.GlobalPose();
    for (const auto& part : component.model->parts)
    {
        DrawItem item;
        item.mesh = part.mesh;
        if (part.material < component.model->materials.size())
            item.tint = component.model->materials[part.material].baseColor;
        if (part.bones.empty())
            XMStoreFloat4x4(&item.world, XMLoadFloat4x4(&globals.at(part.node)) * world);
        else
        {
            XMStoreFloat4x4(&item.world, world);
            for (const auto& bone : part.bones)
            {
                XMFLOAT4X4 matrix;
                XMStoreFloat4x4(&matrix, XMLoadFloat4x4(&bone.inverseBind) *
                                             XMLoadFloat4x4(&globals.at(bone.node)));
                item.bones.push_back(matrix);
            }
        }
        frame.items.push_back(std::move(item));
    }
}
} // namespace nereides
