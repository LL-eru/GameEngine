param(
    [string]$Target = "",
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [ValidateSet("x64", "Win32")]
    [string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"

if ($Host.Name -eq 'ConsoleHost') {
    chcp 65001 | Out-Null
    [Console]::OutputEncoding = [System.Text.Encoding]::UTF8
    [Console]::InputEncoding = [System.Text.Encoding]::UTF8
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$workspaceRoot = Split-Path -Parent $scriptRoot

if ($env:VULKAN_SDK) {
    $env:VulkanSDK14 = $env:VULKAN_SDK
} elseif ($env:VulkanSDK14 -match '[/\\]Bin[/\\]?$') {
    $env:VulkanSDK14 = Split-Path -Parent $env:VulkanSDK14.TrimEnd('\')
}

if (-not $env:VulkanSDK14) {
    Write-Warning "VulkanSDK14 is not set. Set it to your Vulkan SDK root (e.g. C:\VulkanSDK\1.4.341.1)."
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    Write-Error "vswhere.exe not found. Install Visual Studio 2022 with the C++ workload."
}

$msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
if (-not $msbuild -or -not (Test-Path $msbuild)) {
    Write-Error "MSBuild.exe not found. Install Visual Studio 2022 with the C++ workload."
}

$solutionDir = "$workspaceRoot\"
$commonArgs = @(
    "/m",
    "/p:Configuration=$Configuration",
    "/p:Platform=$Platform",
    "/p:SolutionDir=$solutionDir",
    "/consoleloggerparameters:NoSummary"
)

function Invoke-ProjectBuild {
    param([string]$ProjectName)

    $projectFile = Join-Path $workspaceRoot "$ProjectName\$ProjectName.vcxproj"
    if (-not (Test-Path $projectFile)) {
        Write-Error "Project not found: $projectFile"
    }

    $args = @($projectFile) + $commonArgs
    Write-Host "MSBuild: $msbuild"
    Write-Host "Args: $($args -join ' ')"

    & $msbuild @args
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

if ($Target) {
    $projectNames = $Target.Split(';', [StringSplitOptions]::RemoveEmptyEntries) | ForEach-Object { $_.Trim() }
    foreach ($projectName in $projectNames) {
        Invoke-ProjectBuild -ProjectName $projectName
    }
    exit 0
}

$solution = Join-Path $workspaceRoot "GameEngine.sln"
if (-not (Test-Path $solution)) {
    Write-Error "Solution not found: $solution"
}

$msbuildArgs = @($solution) + $commonArgs
Write-Host "MSBuild: $msbuild"
Write-Host "Args: $($msbuildArgs -join ' ')"

& $msbuild @msbuildArgs
exit $LASTEXITCODE
