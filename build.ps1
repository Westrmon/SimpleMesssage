#requires -Version 5.1
<#
.SYNOPSIS
    SimpleMessage 构建脚本。
.EXAMPLE
    .\build.ps1
    .\build.ps1 -Clean -Test
    .\build.ps1 -Config Debug -BuildDir build-debug -Jobs 8
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Config = 'Release',

    [string]$BuildDir = 'build',

    [int]$Jobs = 0,

    [switch]$Clean,
    [switch]$Test,
    [switch]$NoTests,
    [switch]$NoSamples,
    [switch]$Win7Compat,
    [switch]$Help
)

$ErrorActionPreference = 'Stop'

function Show-Usage {
    Write-Host @'
用法: .\build.ps1 [参数]

参数:
  -Config <名称>       构建配置，默认 Release
  -BuildDir <目录>     构建目录，默认 <仓库>\build
  -Jobs <数量>         并行任务数，默认交给 CMake 决定
  -Clean               先删除已有的构建目录
  -Test                构建后运行 ctest
  -NoTests             不构建单元测试
  -NoSamples           不构建示例插件
  -Win7Compat          开启 SM_WIN7_COMPAT
  -Help                显示本帮助

示例:
  .\build.ps1 -Clean -Test
  .\build.ps1 -Config Debug -BuildDir build-debug
'@
}

if ($Help) {
    Show-Usage
    exit 0
}

if ($Test -and $NoTests) {
    Write-Error '-Test 与 -NoTests 不能同时使用。'
    exit 2
}

if ($null -eq (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Error '找不到 cmake，请先安装 CMake 3.20 或更高版本。'
    exit 1
}

$root = $PSScriptRoot
if ([string]::IsNullOrEmpty($root)) {
    $root = (Get-Location).Path
}

if (-not [System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir = Join-Path $root $BuildDir
}
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)

if ($Clean -and (Test-Path -LiteralPath $BuildDir)) {
    $cache = Join-Path $BuildDir 'CMakeCache.txt'
    $isEmpty = -not (Get-ChildItem -LiteralPath $BuildDir -Force | Select-Object -First 1)
    if (Test-Path -LiteralPath $cache) {
        Remove-Item -LiteralPath $BuildDir -Recurse -Force
    } elseif ($isEmpty) {
        Remove-Item -LiteralPath $BuildDir -Force
    } else {
        Write-Error "$BuildDir 不是 CMake 构建目录（缺少 CMakeCache.txt），拒绝删除。"
        exit 1
    }
}

$isWindows = [System.Environment]::OSVersion.Platform -eq [System.PlatformID]::Win32NT
$buildTests = if ($NoTests) { 'OFF' } else { 'ON' }
$buildSamples = if ($NoSamples) { 'OFF' } else { 'ON' }

$configure = @('-S', $root, '-B', $BuildDir)
if (-not $isWindows) {
    $configure += "-DCMAKE_BUILD_TYPE=$Config"
}
$configure += "-DSM_BUILD_TESTS=$buildTests"
$configure += "-DSM_BUILD_SAMPLES=$buildSamples"
if ($Win7Compat) {
    $configure += '-DSM_WIN7_COMPAT=ON'
}

Write-Host "[build] 配置: $Config"
Write-Host "[build] 目录: $BuildDir"
& cmake @configure
if ($LASTEXITCODE -ne 0) {
    Write-Error "CMake 配置失败（退出码 $LASTEXITCODE）。"
    exit $LASTEXITCODE
}

$buildArgs = @('--build', $BuildDir, '--config', $Config, '--parallel')
if ($Jobs -gt 0) {
    $buildArgs += $Jobs
}

Write-Host "[build] 编译 (配置 $Config)"
& cmake @buildArgs
if ($LASTEXITCODE -ne 0) {
    Write-Error "CMake 构建失败（退出码 $LASTEXITCODE）。"
    exit $LASTEXITCODE
}

$configDir = Join-Path $BuildDir $Config
$artifactDir = if (Test-Path -LiteralPath $configDir) { $configDir } else { $BuildDir }
Write-Host "[build] 产物目录: $artifactDir"

if ($Test) {
    Write-Host '[build] 运行测试'
    & ctest --test-dir $BuildDir -C $Config --output-on-failure
    if ($LASTEXITCODE -ne 0) {
        Write-Error "测试失败（退出码 $LASTEXITCODE）。"
        exit $LASTEXITCODE
    }
}
