param(
    [string]$Root = (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path))
)

$ErrorActionPreference = "Stop"
$utf8 = [Text.Encoding]::UTF8
$sjis = [Text.Encoding]::GetEncoding(932)
$utf8bom = New-Object Text.UTF8Encoding $true

function Test-HasUtf8Bom([byte[]]$Bytes) {
    return $Bytes.Length -ge 3 -and $Bytes[0] -eq 0xEF -and $Bytes[1] -eq 0xBB -and $Bytes[2] -eq 0xBF
}

function Get-JapaneseScore([string]$Text) {
    return ([regex]::Matches($Text, '[\u3040-\u30ff\u4e00-\u9fff]')).Count
}

function Test-ValidUtf8([byte[]]$Bytes) {
    try {
        $decoder = [Text.UTF8Encoding]::new($false, $true)
        $null = $decoder.GetString($Bytes)
        return $true
    }
    catch {
        return $false
    }
}

function Save-Utf8Bom([string]$Path, [string]$Text) {
    [IO.File]::WriteAllText($Path, $Text, $utf8bom)
}

$dirs = @("Editor", "Vulkan", "Interface", "Game", "ShaderTool")
$files = foreach ($dir in $dirs) {
    $path = Join-Path $Root $dir
    if (Test-Path $path) {
        Get-ChildItem -Path $path -Recurse -Include *.cpp, *.cxx, *.hxx, *.h, *.hpp -File
    }
}

foreach ($file in $files) {
    $bytes = [IO.File]::ReadAllBytes($file.FullName)
    if (Test-HasUtf8Bom $bytes) { continue }

    $hasHigh = $false
    foreach ($b in $bytes) { if ($b -gt 127) { $hasHigh = $true; break } }

    if (-not $hasHigh) {
        Save-Utf8Bom $file.FullName ($utf8.GetString($bytes))
        Write-Host "BOM added: $($file.Name)"
        continue
    }

    if (Test-ValidUtf8 $bytes) {
        Save-Utf8Bom $file.FullName ($utf8.GetString($bytes))
        Write-Host "UTF-8 BOM added: $($file.Name)"
        continue
    }

    $textSjis = $sjis.GetString($bytes)
    Save-Utf8Bom $file.FullName $textSjis
    Write-Host "Shift-JIS -> UTF-8 BOM: $($file.Name)"
}

Write-Host "Done."
