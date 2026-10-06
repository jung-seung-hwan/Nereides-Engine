param([string[]]$Configurations = @('Debug', 'Release'))
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$msbuild = & $vswhere -version '[17.0,18.0)' -requires Microsoft.Component.MSBuild -find 'MSBuild/Current/Bin/MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) { throw 'VS2022 MSBuild not found' }
Push-Location $root
try {
    foreach ($config in $Configurations) {
        if ($config -notin @('Debug','Release')) { throw 'Unknown build configuration' }
        & $msbuild NereidesEngine.sln "/p:Configuration=$config" /p:Platform=x64 /v:minimal /nologo
        if ($LASTEXITCODE -ne 0) { throw "$config build failed" }
        foreach ($mode in @('--engine-tests','--smoke-test','--scene-smoke-test','--editor-smoke-test','--presentation-smoke-test','--resize-smoke-test')) {
            $process = Start-Process -FilePath (Join-Path $root "bin/x64/$config/NereidesSandbox.exe") -ArgumentList $mode -PassThru -WindowStyle Hidden
            if (-not $process.WaitForExit(20000)) {
                $process.Kill()
                throw "$config $mode timed out"
            }
            $process.Refresh()
            if ($process.ExitCode -ne 0) { throw "$config $mode failed: $($process.ExitCode)" }
            Write-Output "$config $mode PASS"
        }
    }
} finally { Pop-Location }
