#Requires -Version 5.1
<#
.SYNOPSIS
    Собирает серверный пакет DiDog (zip) из готового dist\MessengerApp.
.DESCRIPTION
    Включает серверный бинарник, TLS-ключи, зависимости Qt и README.
    Пакет можно распаковать на VPS или Windows-сервер и запустить
    MessengerServer.exe (--install-service для службы Windows).
.EXAMPLE
    .\scripts\package-server.ps1 -Version 2.5.0
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Version,
    [string]$Dist = 'dist\MessengerApp',
    [string]$OutputDir = 'dist'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $root $Dist
$outDir = Join-Path $root $OutputDir
$stage = Join-Path $outDir "DiDog-Server-$Version"
$zip = Join-Path $outDir "DiDog-Server-$Version.zip"

if (-not (Test-Path (Join-Path $source 'MessengerServer.exe'))) {
    throw "Не найден MessengerServer.exe в $source."
}

Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item $zip -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $stage | Out-Null

Copy-Item (Join-Path $source '*') $stage -Recurse -Force
if (Test-Path (Join-Path $stage 'MessengerClient.exe')) {
    Remove-Item (Join-Path $stage 'MessengerClient.exe') -Force
}
if (Test-Path (Join-Path $stage 'MessengerUpdater.exe')) {
    Remove-Item (Join-Path $stage 'MessengerUpdater.exe') -Force
}

@"
DiDog Server $Version
=====================

Windows:
  MessengerServer.exe --port 45000 --data C:\ProgramData\DiDog --tls
  MessengerServer.exe --install-service --port 45000 --data C:\ProgramData\DiDog --tls

Linux:
  см. docs\VPS-DEPLOY.md

TLS-файлы: tls\server.crt, tls\server.key (самоподписанные, замените на свои).
Клиенты подключаются по отпечатку сертификата (TOFU).
"@ | Set-Content -Path (Join-Path $stage 'SERVER-README.txt') -Encoding UTF8

Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -CompressionLevel Optimal
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
Write-Host ("Серверный пакет: {0} ({1} МБ)" -f $zip, [math]::Round((Get-Item $zip).Length / 1MB, 1))
