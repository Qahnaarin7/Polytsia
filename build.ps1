param(
    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'build'),
    [string]$VisualStudio,
    [string]$WindowsSdkVersion
)

$ErrorActionPreference = 'Stop'
$programsX86 = [Environment]::GetFolderPath('ProgramFilesX86')
$sdkRoot = Join-Path $programsX86 'Windows Kits\10'
if (-not $VisualStudio) {
    $vswhere = Join-Path $programsX86 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        foreach ($install in (& $vswhere -latest -products '*' -property installationPath)) {
            if (Test-Path -LiteralPath (Join-Path $install 'VC\Tools\MSVC')) { $VisualStudio = $install; break }
        }
    }
    if (-not $VisualStudio) {
        foreach ($base in @($env:ProgramFiles, $programsX86)) {
            $versions = Get-ChildItem -LiteralPath (Join-Path $base 'Microsoft Visual Studio') -Directory -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -match '^\d{2,4}$' } | Sort-Object { [int]$_.Name } -Descending
            foreach ($version in $versions) {
                foreach ($edition in (Get-ChildItem -LiteralPath $version.FullName -Directory)) {
                    if (Test-Path -LiteralPath (Join-Path $edition.FullName 'VC\Tools\MSVC')) {
                        $VisualStudio = $edition.FullName
                        break
                    }
                }
                if ($VisualStudio) { break }
            }
            if ($VisualStudio) { break }
        }
    }
}
if (-not $VisualStudio) { throw 'Install MSVC Build Tools with the Desktop development with C++ workload.' }

$compiler = Get-ChildItem -LiteralPath (Join-Path $VisualStudio 'VC\Tools\MSVC') -Directory |
    Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'bin\HostX64\x64\cl.exe') } |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (-not $compiler) { throw 'The selected Visual Studio installation has no x64 MSVC compiler.' }

if (-not $WindowsSdkVersion) {
    $WindowsSdkVersion = Get-ChildItem -LiteralPath (Join-Path $sdkRoot 'Include') -Directory |
        Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' -and [version]$_.Name -ge [version]'10.0.22621.0' -and
            (Test-Path -LiteralPath (Join-Path $sdkRoot "Lib\$($_.Name)\um\x64\winsqlite3.lib")) -and
            (Test-Path -LiteralPath (Join-Path $sdkRoot "bin\$($_.Name)\x64\rc.exe")) } |
        Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1 -ExpandProperty Name
}
if (-not $WindowsSdkVersion) { throw 'Install a Windows 11 SDK, version 10.0.22621.0 or later.' }

$output = (New-Item -ItemType Directory -Force -Path $OutputDirectory).FullName
$vcBin = Join-Path $compiler.FullName 'bin\HostX64\x64'
$sdkBin = Join-Path $sdkRoot "bin\$WindowsSdkVersion\x64"
$sdkInclude = Join-Path $sdkRoot "Include\$WindowsSdkVersion"
$sdkLib = Join-Path $sdkRoot "Lib\$WindowsSdkVersion"
$oldInclude = $env:INCLUDE
$oldLib = $env:LIB
$oldPath = $env:PATH

Push-Location $PSScriptRoot
try {
    $env:PATH = "$vcBin;$sdkBin;$oldPath"
    $env:INCLUDE = "$($compiler.FullName)\include;$sdkInclude\ucrt;$sdkInclude\shared;$sdkInclude\um;$sdkInclude\winrt"
    $env:LIB = "$($compiler.FullName)\lib\x64;$sdkLib\ucrt\x64;$sdkLib\um\x64"
    $resource = Join-Path $output 'app.res'
    & (Join-Path $sdkBin 'rc.exe') /nologo /I. "/fo$resource" app.rc
    if ($LASTEXITCODE) { throw "Resource compiler exited with code $LASTEXITCODE." }
    $arguments = @(
        '/nologo', '/std:c++20', '/EHsc', '/utf-8', '/O2', '/MT', '/W4', '/GS', '/Gy',
        '/DUNICODE', '/D_UNICODE', '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX', '/D_WIN32_WINNT=0x0A00', "/Fo$output\\",
        'app.cpp', 'view.cpp', 'graphics.cpp', 'library.cpp', 'cover_editor.cpp', 'games.cpp', 'log.cpp', 'actions.cpp', 'window.cpp', $resource,
        '/link', '/MANIFEST:NO', '/SUBSYSTEM:WINDOWS', '/OPT:REF', '/OPT:ICF', "/OUT:$output\Polytsia.exe",
        'user32.lib', 'gdi32.lib', 'dwmapi.lib', 'd2d1.lib', 'dwrite.lib', 'winsqlite3.lib', 'shell32.lib', 'ole32.lib',
        'uuid.lib', 'comctl32.lib', 'advapi32.lib', 'windowscodecs.lib', 'uxtheme.lib', 'powrprof.lib',
        'propsys.lib', 'shlwapi.lib'
    )
    & (Join-Path $vcBin 'cl.exe') @arguments
    if ($LASTEXITCODE) { throw "MSVC exited with code $LASTEXITCODE." }
} finally {
    Pop-Location
    $env:INCLUDE = $oldInclude
    $env:LIB = $oldLib
    $env:PATH = $oldPath
}
Write-Output (Join-Path $output 'Polytsia.exe')
