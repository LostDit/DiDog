#Requires -Version 5.1
<#
.SYNOPSIS
    Готовит манифест обновления, упаковывает dist в tar.gz и отправляет на сервер.
.EXAMPLE
    .\scripts\publish-update.ps1 -Version 2.1.0 -Server 51.250.76.250 -User lostdead
.EXAMPLE
    .\scripts\publish-update.ps1 -Version 2.1.0 -Server 51.250.76.250 -Deploy -RemoteScript /opt/messengerapp/deploy-update.sh
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Version,

    [string]$Server = '',
    [string]$User = 'lostdead',
    [string]$Dist = 'dist\MessengerApp',
    [string]$RemoteDir = '/tmp',

    # Если задан и указан -Deploy, вызывается по SSH после загрузки.
    [string]$RemoteScript = '',

    [switch]$Deploy,
    [switch]$IncludeUpdater,
    [switch]$IncludeServer,
    [switch]$IncludeBridge,
    [switch]$DryRun,

    # Обновить и серверный бинарник (git pull + сборка + установка службы).
    [switch]$ServerUpdate,
    [string]$ServerRepo = '~/MessengerApp'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$distPath = $Dist
if (-not [System.IO.Path]::IsPathRooted($distPath)) {
    $distPath = Join-Path $root $Dist
}
if (-not (Test-Path $distPath)) {
    throw "Каталог не найден: $distPath. Соберите Release и пакет."
}

# Манифест: по умолчанию без updater и серверного бинарника.
$exclude = @()
if (-not $IncludeUpdater) {
    $exclude += 'MessengerUpdater.exe'
}
if (-not $IncludeServer) {
    $exclude += 'MessengerServer.exe'
}

$excludeDirs = @()
if (-not $IncludeBridge) {
    $excludeDirs += 'bridge'
}

Write-Host "== Манифест =="
& (Join-Path $PSScriptRoot 'make-manifest.ps1') `
    -Version $Version -Dist $distPath -Exclude $exclude -ExcludeDirs $excludeDirs
if (-not $?) {
    throw "make-manifest.ps1 завершился с ошибкой."
}

$manifestPath = Join-Path $distPath 'manifest.json'
$manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json
Write-Host ("Версия: {0}, файлов: {1}" -f $manifest.version, $manifest.files.Count)

if ([string]$manifest.version -ne $Version) {
    throw "Версия в манифесте ($($manifest.version)) не совпадает с $Version."
}

# Проверка размеров и хешей перед упаковкой.
$bad = 0
foreach ($file in $manifest.files) {
    $path = Join-Path $distPath ($file.path -replace '/', '\')
    if (-not (Test-Path $path)) {
        Write-Host "MISSING $($file.path)"
        $bad++
        continue
    }
    if ((Get-Item $path).Length -ne $file.size) {
        Write-Host "SIZE MISMATCH $($file.path)"
        $bad++
        continue
    }
    $hash = (Get-FileHash $path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -ne $file.sha256) {
        Write-Host "HASH MISMATCH $($file.path)"
        $bad++
    }
}
if ($bad -ne 0) {
    throw "Манифест не совпадает с файлами: $bad ошибок."
}
Write-Host "Хеши: OK"

$archive = Join-Path $root ("dist\messenger-updates-{0}.tar.gz" -f $Version)
$parent = Split-Path $distPath -Parent
$leaf = Split-Path $distPath -Leaf

Write-Host "== Архив =="
if (Test-Path $archive) {
    Remove-Item $archive -Force
}
& tar -czf $archive -C $parent $leaf
if ($LASTEXITCODE -ne 0) {
    throw "Не удалось создать архив (нужен tar.exe из Windows 10+)."
}
$sizeMb = [math]::Round((Get-Item $archive).Length / 1MB, 1)
Write-Host ("Архив: {0} ({1} МБ)" -f $archive, $sizeMb)

if ($DryRun -or -not $Server) {
    Write-Host "== Готово (без отправки) =="
    if (-not $Server) {
        Write-Host "Укажите -Server, чтобы отправить архив."
    }
    return
}

$target = "{0}@{1}" -f $User, $Server
$remoteArchive = "$RemoteDir/messenger-updates-$Version.tar.gz"

Write-Host "== Отправка =="
& scp $archive "${target}:${RemoteDir}/"
if ($LASTEXITCODE -ne 0) {
    throw "scp завершился с ошибкой. Проверьте SSH-ключ: ssh $target `"echo ok`""
}
Write-Host "Загружено: $remoteArchive"

if ($Deploy) {
    if (-not $RemoteScript) {
        throw "Для -Deploy укажите -RemoteScript (путь к deploy-update.sh на сервере)."
    }

    Write-Host "== Установка на сервере =="
    & ssh $target "sudo bash $RemoteScript $remoteArchive"
    if ($LASTEXITCODE -ne 0) {
        throw "Серверный скрипт завершился с ошибкой."
    }
}

if ($ServerUpdate) {
    Write-Host "== Обновление серверного бинарника =="
    $remote = "cd $ServerRepo && git pull && cmake --build build -j`$(nproc) && " +
              "sudo systemctl stop messengerapp && " +
              "sudo install -m 755 build/MessengerServer /opt/messengerapp/MessengerServer && " +
              "sudo systemctl start messengerapp"
    & ssh $target $remote
    if ($LASTEXITCODE -ne 0) {
        throw "Обновление сервера завершилось с ошибкой."
    }
}

Write-Host "== Готово =="
