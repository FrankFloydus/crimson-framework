#Requires -Version 7.0
# Copyright Francesco Di Blasi 2026 - All Rights Reserved

<#
.SYNOPSIS
Wrap an exported Windows executable for an authorized Steam app.
.DESCRIPTION
SteamCMD sends the executable to Valve and writes a separate wrapped executable.
The Steam account must have permission to wrap the app. SteamCMD handles authentication.
.PARAMETER Account
SteamCMD login name. Defaults to CRIMSON_STEAM_RELEASE_ACCOUNT.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$SteamCmd,
    [Parameter(Mandatory)][uint32]$AppId,
    [Parameter(Mandatory)][string]$InputExe,
    [Parameter(Mandatory)][string]$OutputExe,
    [ValidateSet('Normal', 'Compatibility')][string]$Mode = 'Normal',
    [string]$Account = $env:CRIMSON_STEAM_RELEASE_ACCOUNT
)

$ErrorActionPreference = 'Stop'
if ($AppId -eq 0 -or $AppId -eq 480) {
    throw 'Supply the AppID of a game your Steam account can wrap.'
}
if ($Account -cnotmatch '^[A-Za-z0-9_]{3,64}$') {
    throw 'Supply a SteamCMD login name through -Account or CRIMSON_STEAM_RELEASE_ACCOUNT.'
}

$tool = Get-Item -LiteralPath $SteamCmd
$source = Get-Item -LiteralPath $InputExe
$parent = Get-Item -LiteralPath (Split-Path -Parent ([IO.Path]::GetFullPath($OutputExe, $PWD.ProviderPath)))
$destination = Join-Path $parent.FullName ([IO.Path]::GetFileName($OutputExe))
if ($tool -isnot [IO.FileInfo] -or $tool.Extension -ine '.exe' -or
    $source -isnot [IO.FileInfo] -or $source.Extension -ine '.exe' -or $source.Length -eq 0 -or
    $parent -isnot [IO.DirectoryInfo] -or [IO.Path]::GetExtension($destination) -ine '.exe') {
    throw 'Provide SteamCMD, a nonempty input executable, and an output executable path in an existing folder.'
}
if (Test-Path -LiteralPath $destination) {
    throw 'The wrapped output path must be a new file.'
}

$stageDirectory = (New-Item -ItemType Directory -Path (Join-Path $parent.FullName (
    '.steam-wrap-' + [guid]::NewGuid().ToString('N')))).FullName
$stageFile = Join-Path $stageDirectory 'wrapped.exe'
try {
    $flags = if ($Mode -eq 'Compatibility') { '6' } else { '0' }
    $start = [Diagnostics.ProcessStartInfo]::new($tool.FullName)
    $start.UseShellExecute = $false
    $start.WorkingDirectory = $tool.DirectoryName
    foreach ($argument in @('+login', $Account, '+drm_wrap', [string]$AppId, $source.FullName,
            $stageFile, 'drmtoolp', $flags, '+quit')) {
        $start.ArgumentList.Add($argument)
    }
    $process = [Diagnostics.Process]::Start($start)
    try {
        $process.WaitForExit()
        $nativeExit = $process.ExitCode
    } finally {
        $process.Dispose()
    }
    if ($nativeExit -ne 0) {
        throw "SteamCMD DRM wrapping failed with exit code $nativeExit."
    }
    $wrapped = Get-Item -LiteralPath $stageFile -ErrorAction SilentlyContinue
    if ($wrapped -isnot [IO.FileInfo] -or $wrapped.Length -eq 0 -or
        ($wrapped.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'SteamCMD did not produce a wrapped executable.'
    }
    [IO.File]::Move($stageFile, $destination, $false)
    Write-Output $destination
} finally {
    if (Test-Path -LiteralPath $stageFile) {
        Remove-Item -LiteralPath $stageFile -Force -ErrorAction SilentlyContinue
    }
    if (!(Get-ChildItem -LiteralPath $stageDirectory -Force)) {
        Remove-Item -LiteralPath $stageDirectory -ErrorAction SilentlyContinue
    }
}
