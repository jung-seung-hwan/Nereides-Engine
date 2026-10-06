param([string]$VcpkgRoot = $env:VCPKG_ROOT)
$ErrorActionPreference = 'Stop'
if (-not $VcpkgRoot) { throw 'Pass -VcpkgRoot pointing to your vcpkg checkout.' }
$root = Split-Path $PSScriptRoot -Parent
& (Join-Path $VcpkgRoot 'vcpkg.exe') install --triplet x64-windows-v143 "--overlay-triplets=$root/triplets" "--x-manifest-root=$root" "--x-install-root=$root/vcpkg_installed"
if ($LASTEXITCODE -ne 0) { throw 'Dependency installation failed' }
