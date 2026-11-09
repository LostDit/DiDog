#Requires -Version 5.1
<#
.SYNOPSIS
    Создаёт manifest.json для автообновления по содержимому каталога.
.EXAMPLE
    .\scripts\make-manifest.ps1 -Version 1.8.0 -Dist dist\MessengerApp
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Version,

    [string]$Dist = 'dist\MessengerApp',
    [string]$Output = '',
    # Файлы, которые не попадают в манифест (например, MessengerUpdater.exe).
    [string[]]$Exclude = @(),
    # Каталоги (относительно Dist), которые не попадают в манифест.
    [string[]]$ExcludeDirs = @()
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

if (-not [System.IO.Path]::IsPathRooted($Dist)) {
    $Dist = Join-Path $root $Dist
}
if (-not (Test-Path $Dist)) {
    throw "Каталог не найден: $Dist"
}
if (-not $Output) {
    $Output = Join-Path $Dist 'manifest.json'
}

$files = @()
$skippedEmpty = 0
Get-ChildItem $Dist -Recurse -File |
    Where-Object { $_.Name -ne 'manifest.json' -and ($Exclude -notcontains $_.Name) } |
    ForEach-Object {
        $relative = $_.FullName.Substring($Dist.Length + 1).Replace('\', '/')
        $excluded = $false
        foreach ($dir in $ExcludeDirs) {
            if ($relative -eq $dir -or $relative.StartsWith("$dir/")) {
                $excluded = $true
                break
            }
        }
        if ($excluded)
            { return }
        if ($_.Length -eq 0) {
            $script:skippedEmpty++
            return
        }
        $hash = (Get-FileHash -Algorithm SHA256 -Path $_.FullName).Hash.ToLowerInvariant()
        $files += [ordered]@{
            path = $relative
            size = $_.Length
            sha256 = $hash
        }
    }
if ($script:skippedEmpty -gt 0) {
    Write-Host "Пропущено пустых файлов: $script:skippedEmpty"
}

$manifest = [ordered]@{
    version = $Version
    files = $files
}

$json = $manifest | ConvertTo-Json -Depth 4
[System.IO.File]::WriteAllText($Output, $json, (New-Object System.Text.UTF8Encoding($false)))

Write-Host "Манифест: $Output"
Write-Host "Версия:   $Version"
Write-Host "Файлов:   $($files.Count)"
