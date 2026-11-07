#Requires -Version 5.1
<#
.SYNOPSIS
    Сборка MessengerApp (MinGW).
.EXAMPLE
    $env:QT_PREFIX = "C:\Qt\Qt5.14.2\5.14.2\mingw73_64"
    .\scripts\build-windows.ps1 -Configuration Debug
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [string]$BuildDir = 'build',

    [string]$QtPrefix = $env:QT_PREFIX
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

if (-not $QtPrefix) {
    $qmake = Get-Command qmake -ErrorAction SilentlyContinue
    if ($qmake) {
        $QtPrefix = Split-Path -Parent (Split-Path -Parent $qmake.Source)
    }
}

if (-not $QtPrefix -or -not (Test-Path (Join-Path $QtPrefix 'bin'))) {
    throw "Не найден Qt. Задайте переменную окружения QT_PREFIX, например: `$env:QT_PREFIX='C:\Qt\Qt5.14.2\5.14.2\mingw73_64'"
}

if (-not (Get-Command g++ -ErrorAction SilentlyContinue)) {
    $hint = "Не найден g++ в PATH. Добавьте, например: $env:PATH='C:\Qt\Qt5.14.2\Tools\mingw730_64\bin;' + `$env:PATH"
    throw $hint
}

Write-Host "Qt:     $QtPrefix"
Write-Host "Build:  $BuildDir ($Configuration)"

cmake -S $root -B (Join-Path $root $BuildDir) `
    -G 'MinGW Makefiles' `
    -DCMAKE_BUILD_TYPE=$Configuration `
    -DCMAKE_PREFIX_PATH=$QtPrefix

cmake --build (Join-Path $root $BuildDir) -j 8
Write-Host "Готово: $(Join-Path $root $BuildDir)"
