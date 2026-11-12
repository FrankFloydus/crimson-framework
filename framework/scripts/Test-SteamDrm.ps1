#Requires -Version 7.0
# Copyright Francesco Di Blasi 2026 - All Rights Reserved

<#
.SYNOPSIS
Exercise Windows Steam DRM release orchestration using a compiled local SteamCMD stub.
.DESCRIPTION
Uses the Windows .NET Framework C# compiler and an isolated temporary directory.
No Steam installation, release account, or network access is required.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$compiler = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
if (!(Test-Path -LiteralPath $compiler -PathType Leaf)) {
    throw 'The offline Steam DRM fixture requires the Windows .NET Framework C# compiler.'
}

# Throw with the exact invariant that failed.
function Assert-Fixture {
    param([bool]$Condition, [string]$Description)
    if (!$Condition) { throw $Description }
}

# Run one wrapper invocation and verify its output or expected failure.
function Invoke-WrapCase {
    param(
        [string]$Name,
        [string]$Mode = 'Normal',
        [string]$ExpectedError = '',
        [string]$ExpectedContent = 'crimson-wrapped-fixture',
        [switch]$ExistingOutput,
        [switch]$ExpectNoInvocation,
        [switch]$UseInputAsOutput,
        [switch]$ExpectCollision,
        [uint32]$AppId = 1234567
    )

    $destination = if ($UseInputAsOutput) { $inputExe } else { Join-Path $depot "$Name.exe" }
    if ($ExistingOutput) { [IO.File]::WriteAllText($destination, $ExpectedContent) }
    $start = if (Test-Path -LiteralPath $trace) { @(Get-Content -LiteralPath $trace).Count } else { 0 }
    $env:CRIMSON_STEAM_DRM_FIXTURE_CASE = $Name
    $parameters = @{
        SteamCmd = $stub
        AppId = $AppId
        InputExe = $inputExe
        OutputExe = $destination
        Mode = $Mode
    }
    if ($Mode -eq 'Compatibility') { $parameters.Account = 'crimson_fixture' }

    $failure = $null
    $wrapped = $null
    try {
        $wrapped = & $wrapper @parameters
    } catch {
        $failure = $_.Exception.GetBaseException()
    }

    if ($ExpectCollision) {
        # The native file-exists error text varies with Windows display language.
        Assert-Fixture ($failure -is [IO.IOException] -and ($failure.HResult -band 0xffff) -in 80, 183) (
            "$Name did not return a file-exists error.")
    } elseif ($ExpectedError) {
        Assert-Fixture ($failure -and $failure.Message -like "*$ExpectedError*") (
            "$Name returned unexpected failure '$($failure.Message)'.")
    } else {
        Assert-Fixture (!$failure -and $wrapped -ceq $destination) "$Name did not report the final wrapped output."
    }
    if ($ExpectedContent) {
        Assert-Fixture ((Test-Path -LiteralPath $destination -PathType Leaf) -and
            [IO.File]::ReadAllText($destination) -ceq $ExpectedContent) "$Name did not preserve the expected output bytes."
    } else {
        Assert-Fixture (!(Test-Path -LiteralPath $destination)) "$Name published a failed wrapped output."
    }
    $calls = if (Test-Path -LiteralPath $trace) { @(Get-Content -LiteralPath $trace).Count } else { 0 }
    $expectedCalls = if ($ExpectNoInvocation) { $start } else { $start + 1 }
    Assert-Fixture ($calls -eq $expectedCalls) "$Name did not invoke the local SteamCMD the expected number of times."
    Assert-Fixture ((Get-FileHash -LiteralPath $inputExe).Hash -ceq $inputHash) "$Name changed the input executable."
    Assert-Fixture (!(Get-ChildItem -LiteralPath $depot -Filter '.steam-wrap-*' -Force)) "$Name left a staging directory."
}

$temp = (Get-Item -LiteralPath ([IO.Path]::GetTempPath())).FullName.TrimEnd('\')
$work = (New-Item -ItemType Directory -Path (Join-Path $temp (
    'crimson steam drm ' + [guid]::NewGuid().ToString('N')))).FullName
$envNames = @('CRIMSON_STEAM_DRM_FIXTURE_ROOT', 'CRIMSON_STEAM_DRM_FIXTURE_CASE', 'CRIMSON_STEAM_RELEASE_ACCOUNT')
$previousEnv = @{}
foreach ($name in $envNames) { $previousEnv[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
$passed = $false
try {
    $toolDir = (New-Item -ItemType Directory -Path (Join-Path $work 'steam tool')).FullName
    $inputDir = (New-Item -ItemType Directory -Path (Join-Path $work 'game export')).FullName
    $depot = (New-Item -ItemType Directory -Path (Join-Path $work 'depot build')).FullName
    $inputExe = Join-Path $inputDir 'demo export.exe'
    [IO.File]::WriteAllText($inputExe, 'crimson-export-fixture')
    $inputHash = (Get-FileHash -LiteralPath $inputExe).Hash
    $stub = Join-Path $toolDir 'steamcmd fixture.exe'
    $stubSource = Join-Path $PSScriptRoot 'fixtures/steamcmd_drm_stub.cs'
    & $compiler '/nologo' '/target:exe' "/out:$stub" $stubSource
    Assert-Fixture ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $stub -PathType Leaf)) 'SteamCMD fixture compilation failed.'

    $wrapper = Join-Path $PSScriptRoot 'Wrap-SteamRelease.ps1'
    $trace = Join-Path $work 'native-invocations.txt'
    $env:CRIMSON_STEAM_DRM_FIXTURE_ROOT = $work
    $env:CRIMSON_STEAM_RELEASE_ACCOUNT = 'crimson_fixture'

    Invoke-WrapCase -Name 'normal'
    Invoke-WrapCase -Name 'compatibility' -Mode 'Compatibility'
    Invoke-WrapCase -Name 'native-error' -ExpectedError 'failed with exit code 17' -ExpectedContent ''
    Invoke-WrapCase -Name 'missing-output' -ExpectedError 'did not produce a wrapped executable' -ExpectedContent ''
    Invoke-WrapCase -Name 'empty-output' -ExpectedError 'did not produce a wrapped executable' -ExpectedContent ''
    Invoke-WrapCase -Name 'existing-output' -ExpectedError 'must be a new file' -ExpectedContent 'preserve this output' -ExistingOutput -ExpectNoInvocation
    Invoke-WrapCase -Name 'input-alias' -ExpectedError 'must be a new file' -ExpectedContent 'crimson-export-fixture' -UseInputAsOutput -ExpectNoInvocation
    Invoke-WrapCase -Name 'raced-output' -ExpectCollision -ExpectedContent 'crimson-competing-output'
    Invoke-WrapCase -Name 'spacewar-refused' -AppId 480 -ExpectedError 'account can wrap' -ExpectedContent '' -ExpectNoInvocation
    $passed = $true
    Write-Output 'CRIMSON_STEAM_DRM_PASSED'
} finally {
    foreach ($name in $envNames) { [Environment]::SetEnvironmentVariable($name, $previousEnv[$name], 'Process') }
    $directory = Get-Item -LiteralPath $work
    if ($passed -and $directory.Parent.FullName.TrimEnd('\') -ceq $temp -and
        $directory.Name -cmatch '^crimson steam drm [0-9a-f]{32}$') {
        Remove-Item -LiteralPath $directory.FullName -Recurse -Force
    } elseif (!$passed) {
        Write-Warning "Steam DRM fixture evidence is in $work"
    }
}
