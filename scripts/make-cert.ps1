#Requires -Version 5.1
<#
.SYNOPSIS
    Генерирует самоподписанный сертификат и ключ для TLS-сервера MessengerApp.
.DESCRIPTION
    Нужен OpenSSL 1.1+ в PATH (подойдёт openssl.exe из Git for Windows).
.EXAMPLE
    .\scripts\make-cert.ps1 -OutputDir .\dist\MessengerApp\tls
#>
[CmdletBinding()]
param(
    [string]$OutputDir = '.\dist\MessengerApp\tls',
    [string]$CommonName = 'MessengerApp Server',
    [string[]]$DnsNames = @('localhost'),
    [string[]]$IpAddresses = @('127.0.0.1'),
    [int]$Days = 3650
)

$ErrorActionPreference = 'Stop'

$openssl = (Get-Command openssl -ErrorAction SilentlyContinue).Source
if (-not $openssl) {
    $candidates = @(
        'C:\Program Files\Git\usr\bin\openssl.exe',
        'C:\Program Files\Git\ucrt64\bin\openssl.exe'
    ) | Where-Object { Test-Path $_ }

    if (-not $candidates) {
        throw 'Не найден openssl.exe. Установите OpenSSL или Git for Windows.'
    }
    $openssl = $candidates[0]
}

New-Item -ItemType Directory -Force $OutputDir | Out-Null
$certificate = Join-Path $OutputDir 'server.crt'
$key = Join-Path $OutputDir 'server.key'

$san = @()
$san += $DnsNames | ForEach-Object { "DNS:$_" }
$san += $IpAddresses | ForEach-Object { "IP:$_" }

& $openssl req -x509 -newkey rsa:2048 -sha256 -days $Days -nodes `
    -keyout $key -out $certificate `
    -subj "/CN=$CommonName" -addext ("subjectAltName=" + ($san -join ',')) 2>$null

if (-not (Test-Path $certificate) -or -not (Test-Path $key)) {
    throw 'Не удалось создать сертификат.'
}

$fingerprint = (& $openssl x509 -in $certificate -noout -fingerprint -sha256) -replace '^.*=', ''
$fingerprint = $fingerprint -replace ':', ''

Write-Host "Сертификат: $certificate"
Write-Host "Ключ:       $key"
Write-Host "SHA-256:    $fingerprint"
