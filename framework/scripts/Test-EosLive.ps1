#Requires -Version 7.0
# Copyright Francesco Di Blasi 2026 - All Rights Reserved

[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$GodotExe,
    [switch]$ForceRelay,
    [switch]$NoPrompt,
    [ValidateRange(1, 300)][int]$TimeoutSeconds = 180
)

$ErrorActionPreference = 'Stop'
$engine = (Resolve-Path -LiteralPath $GodotExe).ProviderPath
if (!(Test-Path -LiteralPath $engine -PathType Leaf)) { throw 'Supply the Godot 4.7 console executable.' }
$source = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../project')).ProviderPath
$sourceAddon = Join-Path $source 'addons/crimson-framework'
$addonFiles = @(
    'crimson-framework.gdextension',
    'bin/crimson_framework.windows.template_debug.x86_64.dll',
    'bin/EOSSDK-Win64-Shipping.dll'
)
foreach ($file in $addonFiles) {
    if (!(Test-Path -LiteralPath (Join-Path $sourceAddon $file) -PathType Leaf)) {
        throw "Build the Windows debug addon with EOS enabled. Missing $file."
    }
}

# Read fixture settings locally and mask interactive input.
function Read-EosSetting([string]$Name) {
    $value = [Environment]::GetEnvironmentVariable($Name, 'Process')
    if (![string]::IsNullOrWhiteSpace($value)) { return $value }
    if ($NoPrompt) { throw "Missing environment variable $Name." }
    $masked = Read-Host -Prompt $Name -AsSecureString
    try {
        if ($null -eq $masked -or $masked.Length -eq 0) { throw "Missing value for $Name." }
        return [Net.NetworkCredential]::new('', $masked).Password
    } finally {
        if ($null -ne $masked) { $masked.Dispose() }
    }
}

# Start one isolated Godot process with only its own EOS settings.
function Start-EosChild([string]$Role, [string[]]$Arguments, [hashtable]$Settings = @{}) {
    $start = [Diagnostics.ProcessStartInfo]::new($engine)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.WorkingDirectory = $project
    foreach ($argument in @('--headless', '--path', $project) + $Arguments) {
        $start.ArgumentList.Add($argument)
    }
    foreach ($key in @($start.Environment.Keys)) {
        if ($key.StartsWith('CRIMSON_EOS_', [StringComparison]::OrdinalIgnoreCase)) {
            [void]$start.Environment.Remove($key)
        }
    }
    foreach ($key in $Settings.Keys) { $start.Environment[$key] = $Settings[$key] }
    $userData = Join-Path $area "$Role-data"
    New-Item -ItemType Directory -Path $userData | Out-Null
    $start.Environment['APPDATA'] = $userData
    $start.Environment['LOCALAPPDATA'] = $userData
    try {
        $process = [Diagnostics.Process]::Start($start)
    } finally {
        foreach ($key in $Settings.Keys) { [void]$start.Environment.Remove($key) }
    }
    $child = @{
        Role = $Role
        Process = $process
        Output = $process.StandardOutput.ReadLineAsync()
        ErrorOutput = $process.StandardError.ReadLineAsync()
        Listing = ''
        Path = ''
        Failure = ''
        Passed = $false
    }
    $children.Add($child)
    return $child
}

# Drain bounded batches from both pipes and retain only fixture protocol markers and error categories.
function Update-EosChild([hashtable]$Child) {
    foreach ($pipe in @('Output', 'ErrorOutput')) {
        $reader = if ($pipe -eq 'Output') { $Child.Process.StandardOutput } else { $Child.Process.StandardError }
        for ($index = 0; $index -lt 64 -and $null -ne $Child[$pipe] -and $Child[$pipe].IsCompleted; $index++) {
            $line = $Child[$pipe].GetAwaiter().GetResult()
            if ($null -eq $line) {
                $Child[$pipe] = $null
                break
            }
            $Child[$pipe] = $reader.ReadLineAsync()
            if ($Child.Role -eq 'Import' -and $line -cmatch '^\s*(SCRIPT )?ERROR:') {
                $Child.Failure = 'project import'
            } elseif ($line -cmatch '^\s*ERROR: EOS (developer sign-in|host startup|guest startup) failed') {
                $Child.Failure = $Matches[1]
            } elseif ($pipe -eq 'Output') {
                if ($Child.Role -eq 'Host' -and $line -cmatch '^CRIMSON_EOS_LOBBY_ID=([A-Za-z0-9_-]{1,128})$') {
                    if ($Child.Listing) { throw 'EOS host reported more than one lobby ID.' }
                    $Child.Listing = $Matches[1]
                } elseif ($line -cmatch '^CRIMSON_EOS_PEER_PATH=(direct|relay)$') {
                    $Child.Path = $Matches[1]
                } elseif ($line -ceq ('CRIMSON_EOS_' + $Child.Role.ToUpperInvariant() + '_PASSED')) {
                    $Child.Passed = $true
                }
            }
        }
    }
}

# A child is finished only after its exit status and all pending markers are available.
function Test-EosChildExited([hashtable]$Child) {
    return $Child.Process.HasExited -and $null -eq $Child.Output -and $null -eq $Child.ErrorOutput
}

# Report a bounded, credential-free error if a fixture exited before completing its contract.
function Assert-EosChildPassed([hashtable]$Child) {
    $code = $Child.Process.ExitCode
    if ($Child.Failure) { throw "EOS $($Child.Role) failed during $($Child.Failure) (exit $code)." }
    if ($code -ne 0) { throw "EOS $($Child.Role) exited with code $code." }
    if ($Child.Role -eq 'Import') { return }
    if (!$Child.Passed -or !$Child.Path -or ($Child.Role -eq 'Host' -and !$Child.Listing)) {
        throw "EOS $($Child.Role) exited before reporting all expected test results."
    }
    if ($ForceRelay -and $Child.Path -cne 'relay') {
        throw "EOS $($Child.Role) did not report a relay connection."
    }
}

$shared = @{}
foreach ($name in @('PRODUCT_ID', 'SANDBOX_ID', 'DEPLOYMENT_ID', 'CLIENT_ID', 'CLIENT_SECRET')) {
    $key = "CRIMSON_EOS_$name"
    $shared[$key] = Read-EosSetting $key
}
$devAuthAddress = [Environment]::GetEnvironmentVariable('CRIMSON_EOS_DEV_AUTH_ADDRESS', 'Process')
if (![string]::IsNullOrWhiteSpace($devAuthAddress)) {
    $shared['CRIMSON_EOS_DEV_AUTH_ADDRESS'] = $devAuthAddress
}
$hostSettings = $shared.Clone()
$guestSettings = $shared.Clone()
$hostSettings['CRIMSON_EOS_HOST_CREDENTIAL'] = Read-EosSetting 'CRIMSON_EOS_HOST_CREDENTIAL'
$guestSettings['CRIMSON_EOS_GUEST_CREDENTIAL'] = Read-EosSetting 'CRIMSON_EOS_GUEST_CREDENTIAL'
if ($hostSettings['CRIMSON_EOS_HOST_CREDENTIAL'] -ceq $guestSettings['CRIMSON_EOS_GUEST_CREDENTIAL']) {
    throw 'Use distinct Developer Authentication Tool credential names for the host and guest.'
}

$temporaryRoot = (Resolve-Path -LiteralPath ([IO.Path]::GetTempPath())).ProviderPath.TrimEnd('\', '/')
$areaName = 'crimson-eos-live-' + [guid]::NewGuid().ToString('N')
$area = Join-Path $temporaryRoot $areaName
$project = Join-Path $area 'project'
$addon = Join-Path $project 'addons/crimson-framework'
$children = [Collections.Generic.List[hashtable]]::new()
try {
    New-Item -ItemType Directory -Path (Join-Path $addon 'bin') -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $source 'project.godot'), (Join-Path $source 'demo') -Destination $project -Recurse
    foreach ($file in $addonFiles) {
        Copy-Item -LiteralPath (Join-Path $sourceAddon $file) -Destination (Join-Path $addon $file)
    }
    @'
[debug]
file_logging/enable_file_logging=false
file_logging/enable_file_logging.pc=false
'@ | Set-Content -LiteralPath (Join-Path $project 'override.cfg') -Encoding utf8NoBOM

    $import = Start-EosChild 'Import' @('--editor', '--import')
    $clock = [Diagnostics.Stopwatch]::StartNew()
    do {
        Update-EosChild $import
        if (Test-EosChildExited $import) { break }
        if ($clock.Elapsed.TotalSeconds -ge 120) { throw 'Godot project import timed out.' }
        Start-Sleep -Milliseconds 25
    } while ($true)
    Assert-EosChildPassed $import

    $fixture = @('--frame-delay', '4', '--script', 'res://demo/tests/eos_live.gd', '--')
    if ($ForceRelay) { $fixture += '--force-relay' }
    $clock.Restart()
    $hostChild = Start-EosChild 'Host' ($fixture + '--host') $hostSettings
    $guestChild = $null
    do {
        Update-EosChild $hostChild
        $hostExited = Test-EosChildExited $hostChild
        if ($hostExited) { Assert-EosChildPassed $hostChild }
        if ($null -eq $guestChild -and $hostChild.Listing) {
            $guestChild = Start-EosChild 'Guest' ($fixture + @('--join', $hostChild.Listing)) $guestSettings
        }
        if ($null -ne $guestChild) {
            Update-EosChild $guestChild
            if (Test-EosChildExited $guestChild) {
                Assert-EosChildPassed $guestChild
                if ($hostExited) { break }
            }
        }
        if ($clock.Elapsed.TotalSeconds -ge $TimeoutSeconds) { throw 'EOS host/guest fixture timed out.' }
        Start-Sleep -Milliseconds 25
    } while ($true)

    Write-Output "CRIMSON_EOS_HOST_PEER_PATH=$($hostChild.Path)"
    Write-Output "CRIMSON_EOS_GUEST_PEER_PATH=$($guestChild.Path)"
    Write-Output 'CRIMSON_EOS_HOST_PASSED'
    Write-Output 'CRIMSON_EOS_GUEST_PASSED'
} finally {
    foreach ($child in $children) {
        try {
            if (!$child.Process.HasExited) {
                $child.Process.Kill($true)
                [void]$child.Process.WaitForExit(5000)
            }
        } catch {
            if (!$child.Process.HasExited) { Write-Warning "Could not stop the EOS $($child.Role) process." }
        } finally {
            $child.Process.Dispose()
        }
    }
    $resolved = (Resolve-Path -LiteralPath $area -ErrorAction SilentlyContinue).ProviderPath
    if ($resolved -and [IO.Path]::GetDirectoryName($resolved) -eq $temporaryRoot -and
        [IO.Path]::GetFileName($resolved) -ceq $areaName) {
        Remove-Item -LiteralPath $resolved -Recurse -Force -ErrorAction SilentlyContinue
    }
}
