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
                $null = $node.AppendChild($filter)
            }
            $null = $group.AppendChild($node)
        }
    }
    $xml.Save($path)
}
