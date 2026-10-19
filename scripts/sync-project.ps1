# Keep VS source lists explicit while adding small engine modules.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
foreach ($name in @('NereidesSandbox.vcxproj','NereidesSandbox.vcxproj.filters')) {
    $path = Join-Path $root $name
    [xml]$xml = Get-Content $path
    $ns = $xml.DocumentElement.NamespaceURI
    foreach ($kind in @('ClCompile','ClInclude')) {
        $group = $xml.Project.ItemGroup | Where-Object { $_.$kind } | Select-Object -First 1
        foreach ($old in @($group.SelectNodes("*[local-name()='$kind']"))) { $null = $group.RemoveChild($old) }
        $extension = if ($kind -eq 'ClCompile') { '.cpp' } else { '.h' }
        foreach ($file in Get-ChildItem (Join-Path $root 'src') -Recurse -File | Where-Object Extension -eq $extension | Sort-Object FullName) {
            $node = $xml.CreateElement($kind,$ns)
            $node.SetAttribute('Include',$file.FullName.Substring($root.Length + 1))
            if ($name.EndsWith('.filters')) {
                $filter = $xml.CreateElement('Filter',$ns)
                $filter.InnerText = if ($kind -eq 'ClCompile') {'Source Files'} else {'Header Files'}
                # 렌더러 코드는 src\Renderer 폴더와 같은 이름의 필터에 모음
                if ($file.FullName.StartsWith((Join-Path $root 'src\Renderer'))) { $filter.InnerText += '\Renderer' }
                $null = $node.AppendChild($filter)
            }
            $null = $group.AppendChild($node)
        }
    }
    if ($name.EndsWith('.filters')) {
        $definitions = $xml.Project.ItemGroup | Where-Object { $_.Filter } | Select-Object -First 1
        foreach ($filterName in @('Source Files\Renderer','Header Files\Renderer')) {
            if ($definitions.SelectNodes("*[local-name()='Filter'][@Include='$filterName']").Count -eq 0) {
                $definition = $xml.CreateElement('Filter',$ns)
                $definition.SetAttribute('Include',$filterName)
                $id = $xml.CreateElement('UniqueIdentifier',$ns)
                $id.InnerText = '{' + [guid]::NewGuid().ToString().ToUpper() + '}'
                $null = $definition.AppendChild($id)
                $null = $definitions.AppendChild($definition)
            }
        }
    }
    $xml.Save($path)
}
