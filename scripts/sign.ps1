#Requires -Version 5.1
<#
.SYNOPSIS
    Подписывает exe/установщик сертификатом подписи кода (Authenticode).
.DESCRIPTION
    Нужен сертификат от доверенного УЦ. Самоподписанный сертификат не делает
    издателя «проверенным» для Windows SmartScreen — только для внутренних тестов.
    Пути/пароль берутся из параметров или переменных окружения:
      MESSENGER_SIGN_PFX, MESSENGER_SIGN_PASSWORD, MESSENGER_SIGN_THUMBPRINT.
.EXAMPLE
    .\scripts\sign.ps1 -Files 'dist\MessengerApp\*.exe','dist\MessengerApp\MessengerUpdater.exe' -Installer dist\installer\MessengerApp-Setup-1.8.0.exe
#>
[CmdletBinding()]
param(
    [string[]]$Files = @('dist\MessengerApp\*.exe'),
    [string]$Installer = '',
    [string]$Pfx = $env:MESSENGER_SIGN_PFX,
    [string]$Password = $env:MESSENGER_SIGN_PASSWORD,
    [string]$Thumbprint = $env:MESSENGER_SIGN_THUMBPRINT,
    [string]$TimestampUrl = 'http://timestamp.digicert.com'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

function Resolve-RepoPath([string]$path) {
    if ([System.IO.Path]::IsPathRooted($path)) { return $path }
    return (Join-Path $root $path)
}

$signtool = (Get-Command signtool.exe -ErrorAction SilentlyContinue).Source
if (-not $signtool) {
    $candidates = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin' -Recurse `
        -Filter 'signtool.exe' -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\x64\\' } |
        Sort-Object FullName -Descending
    if ($candidates) { $signtool = $candidates[0].FullName }
}

if (-not $signtool) {
    throw 'Не найден signtool.exe — установите Windows SDK (Signing Tools).'
}

$expanded = @()
foreach ($pattern in $Files) {
    $resolved = Resolve-RepoPath $pattern
    if ($resolved -match '[\*\?]') {
        $expanded += Get-ChildItem $resolved -File | ForEach-Object { $_.FullName }
    } elseif (Test-Path $resolved) {
        $expanded += (Resolve-Path $resolved).Path
    }
}

if ($Installer) {
    $resolvedInstaller = Resolve-RepoPath $Installer
    if (Test-Path $resolvedInstaller) {
        $expanded += (Resolve-Path $resolvedInstaller).Path
    }
}

if ($expanded.Count -eq 0) {
    throw 'Нечего подписывать: файлы не найдены.'
}

if (-not $Pfx -and -not $Thumbprint) {
    throw @'
Нет сертификата подписи кода.
Укажите -Pfx и -Password (или переменные MESSENGER_SIGN_PFX / MESSENGER_SIGN_PASSWORD)
либо -Thumbprint установленного сертификата (MESSENGER_SIGN_THUMBPRINT).
Сертификат должен быть выпущен доверенным УЦ, иначе Windows покажет «Неизвестный издатель».
'@
}

foreach ($file in $expanded) {
    $arguments = @('sign', '/fd', 'SHA256')

    if ($TimestampUrl) {
        $arguments += @('/td', 'SHA256', '/tr', $TimestampUrl)
    }

    if ($Pfx) {
        $arguments += @('/f', (Resolve-RepoPath $Pfx))
        if ($Password) { $arguments += @('/p', $Password) }
    } else {
        $arguments += @('/sha1', $Thumbprint)
    }

    $arguments += $file

    Write-Host "Подпись: $file"
    & $signtool @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "signtool завершился с кодом $LASTEXITCODE для $file"
    }
}

Write-Host "Подписано файлов: $($expanded.Count)"
