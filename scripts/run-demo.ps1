#Requires -Version 5.1
<#
.SYNOPSIS
    Демо: локальный сервер + два клиента с разными профилями.
.DESCRIPTION
    Поднимает MessengerServer и два MessengerClient на одном каталоге данных.
    Данные пишутся в build/demo-data, поэтому не засоряют %APPDATA%.
#>
[CmdletBinding()]
param(
    [int]$Port = 45000,
    [string]$BuildDir = 'build'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $root $BuildDir
$data = Join-Path $root "$BuildDir\demo-data"

$server = Join-Path $bin 'MessengerServer.exe'
$client = Join-Path $bin 'MessengerClient.exe'

if (-not (Test-Path $server) -or -not (Test-Path $client)) {
    throw "Не найдены исполняемые файлы в $bin. Сначала запустите scripts\build-windows.ps1"
}

New-Item -ItemType Directory -Force -Path $data | Out-Null

$serverProcess = Start-Process -FilePath $server `
    -ArgumentList '--port', $Port, '--data', $data -PassThru -WindowStyle Minimized

# Ждём, пока сервер начнёт слушать порт.
$deadline = (Get-Date).AddSeconds(10)
while ((Get-Date) -lt $deadline) {
    $listening = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue
    if ($listening) { break }
    Start-Sleep -Milliseconds 250
}

Write-Host "Сервер запущен на порту $Port (PID $($serverProcess.Id)), данные: $data"
Start-Sleep -Milliseconds 300

$clientA = Start-Process -FilePath $client -ArgumentList '--data', $data, '--profile', 'alice' -PassThru
$clientB = Start-Process -FilePath $client -ArgumentList '--data', $data, '--profile', 'bob' -PassThru

Write-Host "Клиенты: alice (PID $($clientA.Id)), bob (PID $($clientB.Id))"
Write-Host "Зарегистрируйте двух пользователей и напишите друг другу. Ctrl+C — остановить всё."

try {
    Wait-Process -Id $clientA.Id, $clientB.Id
}
finally {
    Get-Process -Id $clientA.Id, $clientB.Id -ErrorAction SilentlyContinue | Stop-Process -Force
    Get-Process -Id $serverProcess.Id -ErrorAction SilentlyContinue | Stop-Process -Force
    Write-Host 'Демо остановлено.'
}
