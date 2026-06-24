param(
    [string]$Root = (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path))
)

$ErrorActionPreference = "Stop"
$utf8 = [Text.Encoding]::UTF8
$sjis = [Text.Encoding]::GetEncoding(932)

function Test-HasUtf8Bom([byte[]]$Bytes) {
    return $Bytes.Length -ge 3 -and $Bytes[0] -eq 0xEF -and $Bytes[1] -eq 0xBB -and $Bytes[2] -eq 0xBF
}

function Test-ValidUtf8([byte[]]$Bytes) {
    try {
        $decoder = [Text.UTF8Encoding]::new($false, $true)
        $null = $decoder.GetString($Bytes)
        return $true
    }
    catch { return $false }
}

function Get-JapaneseScore([string]$Text) {
    return ([regex]::Matches($Text, '[\u3040-\u30ff\u4e00-\u9fff]')).Count
}

function Read-SourceText([byte[]]$Bytes) {
    if ($Bytes.Length -eq 0) { return "" }

    $offset = 0
    if (Test-HasUtf8Bom $Bytes) { $offset = 3 }
    $payload = if ($offset -eq 0) { $Bytes } else { $Bytes[$offset..($Bytes.Length - 1)] }
    if ($payload.Length -eq 0) { return "" }

    $hasHigh = $false
    foreach ($b in $payload) { if ($b -gt 127) { $hasHigh = $true; break } }
    if (-not $hasHigh) { return $utf8.GetString($payload) }

    if (Test-ValidUtf8 $payload) {
        return $utf8.GetString($payload)
    }

    $textSjis = $sjis.GetString($payload)
    $textUtf8 = $utf8.GetString($payload)
    if ((Get-JapaneseScore $textSjis) -ge (Get-JapaneseScore $textUtf8)) {
        return $textSjis
    }
    return $textUtf8
}

$dirs = @("Editor", "Vulkan", "Interface", "Game", "ShaderTool", "Qt")
$files = foreach ($dir in $dirs) {
    $path = Join-Path $Root $dir
    if (Test-Path $path) {
        Get-ChildItem -Path $path -Recurse -Include *.h, *.hxx, *.cpp, *.cxx -File |
            Where-Object { $_.FullName -notmatch '\\obj\\|\\x64\\|\\Debug\\|\\Release\\|\\qt\\' }
    }
}

foreach ($file in $files) {
    $bytes = [IO.File]::ReadAllBytes($file.FullName)
    $text = Read-SourceText $bytes
    [IO.File]::WriteAllText($file.FullName, $text, $sjis)
    Write-Host "Shift-JIS: $($file.FullName.Replace($Root + '\', ''))"
}

Write-Host "Done. Converted $($files.Count) file(s) to Shift-JIS (CP932)."
