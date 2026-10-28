# Проверка согласованности версии: CMakeLists.txt, проект установки и (при выпуске) тег.
# Запускается в CI под PowerShell 7 (pwsh). Параметр -Tag — тег вида v2.0.1.
param(
    [string]$Tag = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

$cmake = Get-Content (Join-Path $root 'CMakeLists.txt') -Raw
if ($cmake -notmatch 'project\(inf_editor VERSION (\d+\.\d+\.\d+)') {
    Write-Output '::error::В CMakeLists.txt не найдена версия проекта'
    exit 1
}
$version = $Matches[1]

$xsp = Get-Content (Join-Path $root 'installer\inf_editor.xsp') -Raw
$errors = @()
if ($xsp -notmatch '(?m)^Version = (\S+)') {
    $errors += 'В installer\inf_editor.xsp не найдена версия продукта'
} elseif ($Matches[1] -ne $version) {
    $errors += "Версия в installer\inf_editor.xsp ($($Matches[1])) не совпадает с CMakeLists.txt ($version)"
}
if ($xsp -notmatch '(?m)^HKLM, "Software\\Microsoft\\MMC\\SnapIns\\[^"]+", Version, REG_SZ, (\S+)') {
    $errors += 'В installer\inf_editor.xsp не найдена версия оснастки в [Registry]'
} elseif ($Matches[1] -ne $version) {
    $errors += "Версия оснастки в [Registry] ($($Matches[1])) не совпадает с CMakeLists.txt ($version)"
}
if ($Tag -and $Tag.TrimStart('v') -ne $version) {
    $errors += "Тег $Tag не совпадает с версией проекта $version"
}

foreach ($e in $errors) {
    Write-Output "::error::$e"
}
if ($errors) {
    exit 1
}
Write-Output "Версия $version"
if ($env:GITHUB_OUTPUT) {
    "version=$version" >> $env:GITHUB_OUTPUT
}
