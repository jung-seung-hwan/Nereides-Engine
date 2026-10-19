#pragma once
#include <cstdint>
#include <span> // C++20: 바이트코드 읽기 전용 인자
#include <string>
#include <vector>
#include "RHI/BindingTypes.h"
#include "RHI/PipelineTypes.h" // ShaderStage

// HLSL → 바이트코드. Device는 바이트코드만 받음 (RHI는 컴파일러를 모름).
//
// 4단계(D12): 빌드 때 fxc로 만든 .cso를 우선 쓰고, Debug 빌드에서 소스 .hlsl이
// .cso보다 새로우면 런타임 컴파일로 대체함. 핫리로드는 Renderer가 원본 파일의
// 수정 시각을 감시하다가 CompileFromFile을 다시 부르는 방식으로 구현함.
namespace ShaderCompiler
{
// D3DCompileFromFile. Debug 빌드는 D3DCOMPILE_DEBUG | SKIP_OPTIMIZATION,
// 그 외는 OPTIMIZATION_LEVEL3. 컴파일러 메시지(줄 번호 포함)를 Log로 보냄.
// path가 절대 경로여야 셰이더 안의 #include가 그 파일 위치를 기준으로 풀림.
// 타깃 프로파일("vs_5_0" 등)은 stage를 보고 구현부가 정함. 호출자는 셰이더 모델을 모름.
bool CompileFromFile(const std::wstring& path, const char* entryPoint, ShaderStage stage,
                     std::vector<uint8_t>& outBytecode);

// 파일을 통째로 읽음(.cso 로드용).
bool LoadFile(const std::wstring& path, std::vector<uint8_t>& outBytes);

// .cso 우선, 필요하면 런타임 컴파일.
//   Debug : 소스 .hlsl(또는 Common.hlsli)이 .cso보다 새로우면 소스를 컴파일함.
//   Release: .cso를 로드함. 없으면 경고를 남기고 exe 옆 .hlsl을 컴파일함.
// hlslName에는 "BasicVertexShader.hlsl" 처럼 파일 이름만 넘김. outSource에는 실제로 사용한 경로가 담김.
bool LoadOrCompile(const wchar_t* hlslName, const char* entryPoint, ShaderStage stage,
                   std::vector<uint8_t>& outBytecode, std::wstring* outSource = nullptr);

// 리플렉션으로 cbuffer 크기를 C++ 구조체 크기와 대조함. 패킹이 어긋나면
// 화면이 조용히 깨지는 대신 여기서 잡힘. 셰이더가 그 cbuffer를 쓰지 않아
// 리플렉션에 없으면 true(검사할 것이 없음)를 돌려주고 Info 로그를 남김.
// C++20 (2026-09-28): 읽기만 하는 바이트코드 인자는 const std::vector<uint8_t>& 대신 std::span<const uint8_t> 로 받음.
// vector 뿐 아니라 C 배열·ShaderDesc::bytecode 도 그대로 들어옴.
bool ValidateConstantBufferSize(std::span<const uint8_t> bytecode, const char* cbufferName,
                                uint32_t expectedSize);

// 셰이더가 실제로 쓰는 바인딩(b#/t#/s#)을 리플렉션으로 읽음.
// Renderer가 직접 선언한 BindingLayout과 대조하는 데 씀.
struct ReflectedBinding
{
    BindingType type;
    uint8_t reg;
    std::string name;
};
bool ReflectBindings(std::span<const uint8_t> bytecode, std::vector<ReflectedBinding>& out);

// 파일의 마지막 수정 시각(FILETIME을 64비트 값으로 변환). 파일이 없으면 0.
uint64_t GetLastWriteTime(const std::wstring& path);
}
