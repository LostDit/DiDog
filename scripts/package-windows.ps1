#Requires -Version 5.1
<#
.SYNOPSIS
    Собирает переносимый дистрибутив MessengerApp для Windows.
.DESCRIPTION
    Копирует исполняемые файлы, зависимости Qt (windeployqt) и переводы в dist/MessengerApp.
    Папку можно переносить на машину без установленного Qt.
.EXAMPLE
    $env:QT_PREFIX = "C:\Qt\Qt5.14.2\5.14.2\mingw73_64"
    .\scripts\package-windows.ps1
#>
[CmdletBinding()]
param(
    [string]$BuildDir = 'build',
    [string]$OutputDir = 'dist',
    [string]$QtPrefix = $env:QT_PREFIX
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root $BuildDir
$dist = Join-Path $root "$OutputDir\MessengerApp"

if (-not (Test-Path (Join-Path $build 'MessengerClient.exe'))) {
    throw "Не найдены собранные файлы в $build. Сначала соберите проект."
}

if (-not $QtPrefix) {
    $qmake = Get-Command qmake -ErrorAction SilentlyContinue
    if ($qmake) {
        $QtPrefix = Split-Path -Parent (Split-Path -Parent $qmake.Source)
    }
}

$windeployqt = if ($QtPrefix) { Join-Path $QtPrefix 'bin\windeployqt.exe' } else { $null }
if (-not $windeployqt -or -not (Test-Path $windeployqt)) {
    $found = Get-Command windeployqt -ErrorAction SilentlyContinue
    if ($found) { $windeployqt = $found.Source }
}

if (-not $windeployqt) {
    throw 'windeployqt не найден: задайте QT_PREFIX или добавьте Qt в PATH'
}

if (Test-Path $dist) {
    Remove-Item $dist -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $dist | Out-Null

Copy-Item (Join-Path $build 'MessengerClient.exe') $dist
Copy-Item (Join-Path $build 'MessengerServer.exe') $dist
$updaterExe = Join-Path $build 'MessengerUpdater.exe'
if (-not (Test-Path $updaterExe)) { $updaterExe = Join-Path $build 'src\MessengerUpdater.exe' }
Copy-Item $updaterExe $dist
Copy-Item (Join-Path $root 'README.md') $dist

# Адрес сервера по умолчанию: клиент прочитает его при первом запуске.
$defaults = Join-Path $root 'messenger.ini'
if (Test-Path $defaults) {
    Copy-Item $defaults $dist
}

# OpenSSL 1.1.1 для TLS (Qt 5.14) и демонстрационный сертификат сервера.
$openssl = Join-Path $root 'third_party\openssl\win64'
if (Test-Path $openssl) {
    Copy-Item (Join-Path $openssl '*.dll') $dist
    Copy-Item (Join-Path $openssl 'LICENSE-openssl.txt') $dist
}

# Opus для голосовых каналов.
$opus = Join-Path $root 'third_party\opus\win64'
if (Test-Path $opus) {
    Copy-Item (Join-Path $opus '*.dll') $dist
    Copy-Item (Join-Path $root 'third_party\opus\LICENSE-opus.txt') $dist
}
$tls = Join-Path $root 'resources\tls'
if (Test-Path $tls) {
    Copy-Item $tls (Join-Path $dist 'tls') -Recurse
}

foreach ($exe in 'MessengerClient.exe', 'MessengerServer.exe') {
    # Без --release/--debug: windeployqt сам определяет тип сборки.
    & $windeployqt --no-translations --dir $dist (Join-Path $dist $exe) | Out-Null
}

$translations = Join-Path $build 'translations'
if (Test-Path $translations) {
    Copy-Item $translations (Join-Path $dist 'translations') -Recurse
}

$size = (Get-ChildItem $dist -Recurse -File | Measure-Object -Property Length -Sum).Sum
Write-Host ("Готово: {0} ({1:N1} МБ)" -f $dist, ($size / 1MB))
Write-Host 'Запуск: MessengerServer.exe --port 45000, затем MessengerClient.exe'
# LiveKit-мост (пилот): кладём рядом с клиентом, чтобы не требовался npm install.
$bridgeSrc = Join-Path $root 'bridge'
$bridgeDst = Join-Path $dist 'bridge'
if (Test-Path $bridgeSrc) {
    Remove-Item $bridgeDst -Recurse -Force -ErrorAction SilentlyContinue
    Copy-Item $bridgeSrc $bridgeDst -Recurse -Force
    Write-Host "LiveKit-мост: $bridgeDst"
}