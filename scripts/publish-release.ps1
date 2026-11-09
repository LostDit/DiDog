#Requires -Version 5.1
<#
.SYNOPSIS
    Публикует установщик DiDog в GitHub Releases.
.DESCRIPTION
    Использует учётные данные Git Credential Manager (те же, что для git push).
    Создаёт релиз по тегу, если его нет, и загружает установщик.
.EXAMPLE
    .\scripts\publish-release.ps1 -Tag v1.1.0 -Notes "Первый релиз с TLS"
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Tag,

    [string]$Repo = 'LostDit/DiDog',
    [string]$Installer,
    [string]$Name,
    [string]$Notes = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

if (-not $Installer) {
    $version = $Tag.TrimStart('v')
    $Installer = Join-Path $root "dist\installer\DiDog-Setup-$version.exe"
}
if (-not (Test-Path $Installer)) {
    throw "Не найден установщик: $Installer (сначала scripts\build-installer.ps1)"
}
if (-not $Name) {
    $Name = "DiDog $($Tag.TrimStart('v'))"
}

$env:GIT_TERMINAL_PROMPT = '0'
$credential = "protocol=https`nhost=github.com`n`n" | git credential fill 2>$null
$token = ($credential | Where-Object { $_ -like 'password=*' } | Select-Object -First 1) -replace '^password=', ''
if (-not $token) {
    throw 'Не удалось получить токен GitHub из Git Credential Manager.'
}

$headers = @{
    Authorization = "token $token"
    Accept = 'application/vnd.github+json'
    'User-Agent' = 'MessengerApp-publish-script'
}

$existing = $null
try {
    $existing = Invoke-RestMethod -Uri "https://api.github.com/repos/$Repo/releases/tags/$Tag" -Headers $headers
} catch {
    $existing = $null
}

if (-not $existing) {
    $payload = @{
        tag_name = $Tag
        target_commitish = 'main'
        name = $Name
        body = $Notes
        draft = $false
        prerelease = $false
    } | ConvertTo-Json -Depth 4

    $bytes = [System.Text.Encoding]::UTF8.GetBytes($payload)
    $existing = Invoke-RestMethod -Method Post `
        -Uri "https://api.github.com/repos/$Repo/releases" `
        -Headers $headers -Body $bytes -ContentType 'application/json; charset=utf-8'

    Write-Host "Создан релиз: $($existing.html_url)"
} else {
    Write-Host "Релиз уже существует: $($existing.html_url)"
}

$assetName = Split-Path -Leaf $Installer
$already = $existing.assets | Where-Object { $_.name -eq $assetName }

if ($already) {
    Write-Host "Файл уже загружен: $($already.browser_download_url)"
} else {
    $uploadUrl = "https://uploads.github.com/repos/$Repo/releases/$($existing.id)/assets?name=$assetName"
    $asset = Invoke-RestMethod -Method Post -Uri $uploadUrl -Headers $headers `
        -ContentType 'application/octet-stream' -InFile $Installer
    Write-Host "Загружено: $($asset.browser_download_url) ($([math]::Round($asset.size / 1MB, 1)) МБ)"
}

Write-Host "Страница релиза: $($existing.html_url)"
