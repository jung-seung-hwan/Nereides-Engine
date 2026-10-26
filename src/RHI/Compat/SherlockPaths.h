#pragma once
#include <string>

// Sherlock "Core/Paths.h" 호환 어댑터. ShaderCompiler/Renderer 가 쓰는 셰이더 경로 함수만 둔다.
// 규칙: 빌드가 만든 .cso 와 원본 사본은 실행 파일 폴더의 Shaders\ 아래에 있다(vcxproj 의 FxCompile / CopyShaderSources).
// Debug 빌드는 소스 트리(NereidesSandbox.vcxproj 가 있는 폴더)의 Shaders\ 를 원본으로 삼아 핫리로드가 저장소 파일을 직접 본다.
namespace Paths
{
// exe\Shaders\<fileName> 의 절대 경로 (원본 폴백).
std::wstring GetShaderPath(const wchar_t* fileName);
// exe\Shaders\<fileName> — 빌드가 만든 .cso.
std::wstring GetShaderBinaryPath(const wchar_t* fileName);
// Debug 핫리로드가 감시하는 원본. Debug 는 소스 트리, 그 밖에는 GetShaderPath와 같다.
std::wstring GetShaderSourcePath(const wchar_t* fileName);
} // namespace Paths
