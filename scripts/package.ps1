param([string]$Name = ('Nereides-' + (Get-Date -Format 'yyyyMMdd-HHmmss')))
$ErrorActionPreference = 'Stop'
if ($Name -notmatch '^[A-Za-z0-9_-]+$') { throw 'Package name must be letters, numbers, underscore or dash' }
$root = Split-Path $PSScriptRoot -Parent
$source = Join-Path $root 'bin/x64/Release'
if (-not (Test-Path (Join-Path $source 'NereidesSandbox.exe'))) { throw 'Build and verify Release first' }
$target = Join-Path $root "dist/$Name"
if (Test-Path -LiteralPath $target) { throw 'Package directory already exists; use a new name' }
$null = New-Item -ItemType Directory -Path $target
Get-ChildItem $source -File | Where-Object { $_.Extension -in '.exe','.dll' } | Copy-Item -Destination $target
Copy-Item (Join-Path $source 'third-party') -Destination $target -Recurse
Copy-Item (Join-Path $root 'data') -Destination $target -Recurse
Copy-Item (Join-Path $root 'README.md') -Destination $target
Copy-Item (Join-Path $root 'docs') -Destination $target -Recurse
Write-Output $target
