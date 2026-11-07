#Requires -Version 5.1
<#
.SYNOPSIS
    Полная сборка установщика MessengerApp для Windows.
.DESCRIPTION
    1) собирает Release-версию;
    2) раскладывает dist\MessengerApp через windeployqt;
    3) компилирует установщик Inno Setup в dist\installer.
.EXAMPLE
    $env:QT_PREFIX = "C:\Qt\Qt5.14.2\5.14.2\mingw73_64"
    .\scripts\build-installer.ps1
#>
[CmdletBinding()]
param(
    [string]$QtPrefix = $env:QT_PREFIX,
    [string]$InnoCompiler = 'C:\Program Files\Inno Setup 7\ISCC.exe'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

if (-not $QtPrefix) {
    throw 'Задайте QT_PREFIX с путём к Qt (каталог с bin\windeployqt.exe).'
}
if (-not (Test-Path $InnoCompiler)) {
    $candidates = Get-ChildItem 'C:\Program Files*' -Filter 'ISCC.exe' -Recurse -Depth 3 -ErrorAction SilentlyContinue
    if ($candidates) { $InnoCompiler = $candidates[0].FullName }
}
if (-not (Test-Path $InnoCompiler)) {
    throw 'Не найден ISCC.exe — установите Inno Setup 6.3+ (winget install JRSoftware.InnoSetup)'
}

Push-Location $root
try {
    Write-Host '== Сборка Release'
    cmake -S . -B build-release -G 'MinGW Makefiles' `
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QtPrefix" -DBUILD_TESTING=OFF | Out-Null
    cmake --build build-release -j 8 | Out-Null

    Write-Host '== Портативный пакет'
    & "$PSScriptRoot\package-windows.ps1" -BuildDir build-release -QtPrefix $QtPrefix

    Write-Host '== Установщик'
    New-Item -ItemType Directory -Force (Join-Path $root 'dist\installer') | Out-Null
    & $InnoCompiler (Join-Path $root 'installer\messengerapp.iss') | Select-Object -Last 3
}
finally {
    Pop-Location
}
