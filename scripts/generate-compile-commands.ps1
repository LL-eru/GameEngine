# Generates compile_commands.json for clangd from vcxproj settings (Debug|x64).
param(
    [string]$WorkspaceRoot = (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path))
)

$ErrorActionPreference = "Stop"

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    Write-Error "vswhere.exe not found."
}

$cl = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -find "VC\Tools\MSVC\*\bin\Hostx64\x64\cl.exe" | Select-Object -First 1
if (-not $cl) {
    Write-Error "cl.exe not found."
}

$root = $WorkspaceRoot.Replace('\', '/')
$corePublic = "$root/Core/Public"
$interface = "$root/Interface"
$glfw = "C:/GLFW/glfw-3.4/include"

$vulkanSdk = $env:VULKAN_SDK
if (-not $vulkanSdk) { $vulkanSdk = $env:VulkanSDK14 }
if (-not $vulkanSdk) { $vulkanSdk = "C:/VulkanSDK/1.4.341.1" }
$vulkanSdk = $vulkanSdk.TrimEnd('\').Replace('\', '/')
$vulkanInclude = "$vulkanSdk/Include"

function New-Entry {
    param(
        [string]$Directory,
        [string]$File,
        [string[]]$Includes,
        [string[]]$Defines,
        [string[]]$ForcedIncludes = @(),
        [string]$SourcePath = ""
    )

    $dir = $Directory.Replace('\', '/')
    $compileArg = $File.Replace('\', '/')
    $fileName = Split-Path $File -Leaf
    $source = if ($SourcePath) { $SourcePath.Replace('\', '/') } else { "$dir/$fileName" }
    $clPath = $cl.Replace('\', '/')

    $args = @("/nologo", "/std:c++20", "/c", $compileArg)
    foreach ($fi in $ForcedIncludes) {
        $args += "/FI$($fi.Replace('\', '/'))"
    }
    foreach ($inc in $Includes) {
        $args += "/I$($inc.Replace('\', '/'))"
    }
    foreach ($def in $Defines) {
        $args += "/D$def"
    }

    return @{
        directory = $dir
        file      = $source
        command   = "$clPath $($args -join ' ')"
    }
}

$commonIncludes = @($corePublic, $interface)
$entries = @()

# Editor
$editorDir = "$root/Editor"
$editorIncludes = @($glfw) + $commonIncludes + @($editorDir)
$editorDefines = @("_DEBUG", "_CONSOLE", "GE_HOST_EDITOR")
foreach ($src in @("main.cpp", "Glfw.cxx", "Plugin.cxx")) {
    $entries += New-Entry -Directory $editorDir -File $src `
        -Includes $editorIncludes -Defines $editorDefines `
        -ForcedIncludes @("$editorDir/absuse.hxx")
}

# Core
$coreDir = "$root/Core"
$coreIncludes = @($coreDir) + $commonIncludes
$coreDefines = @("_DEBUG", "GE_BUILD_CORE")
foreach ($src in @("CoreInit.cxx", "Debugger.cxx", "EngineAllocator.cxx", "Logger.cxx")) {
    $entries += New-Entry -Directory $coreDir -File $src `
        -Includes $coreIncludes -Defines $coreDefines
}

# Vulkan
$vulkanDir = "$root/Vulkan"
$vulkanIncludes = @($vulkanInclude, $glfw) + $commonIncludes + @($vulkanDir)
$vulkanDefines = @("_DEBUG", "_WINDOWS", "GLFW_DLL", "GE_PLUGIN")
$vulkanForced = @(
    "$corePublic/EngineDefines.hxx",
    "$corePublic/EngineLog.hxx",
    "$corePublic/EngineDebug.hxx",
    "$corePublic/EngineMemory.hxx"
)
foreach ($src in @("VulkanRenderer.cxx", "VulkanCommandBuffer.cxx")) {
    $entries += New-Entry -Directory $vulkanDir -File $src `
        -Includes $vulkanIncludes -Defines $vulkanDefines `
        -ForcedIncludes $vulkanForced
}
$entries += New-Entry -Directory $vulkanDir -File "..\Interface\PluginHostContext.cxx" `
    -SourcePath "$interface/PluginHostContext.cxx" `
    -Includes $vulkanIncludes -Defines $vulkanDefines `
    -ForcedIncludes $vulkanForced

# Game
$gameDir = "$root/Game"
$gameIncludes = $commonIncludes + @($gameDir)
$gameDefines = @("_DEBUG", "_CONSOLE", "GE_HOST_GAME")
$entries += New-Entry -Directory $gameDir -File "main.cpp" `
    -Includes $gameIncludes -Defines $gameDefines `
    -ForcedIncludes $vulkanForced

# ShaderTool
$shaderDir = "$root/ShaderTool"
$shaderIncludes = $commonIncludes + @($shaderDir)
$shaderDefines = @("_DEBUG", "_CONSOLE")
$entries += New-Entry -Directory $shaderDir -File "main.cpp" `
    -Includes $shaderIncludes -Defines $shaderDefines `
    -ForcedIncludes $vulkanForced

$output = Join-Path $WorkspaceRoot "compile_commands.json"
$json = $entries | ConvertTo-Json -Depth 4
[System.IO.File]::WriteAllText($output, $json, (New-Object System.Text.UTF8Encoding $false))
Write-Host "Wrote $($entries.Count) entries to $output"
