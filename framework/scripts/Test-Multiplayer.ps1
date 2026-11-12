#Requires -Version 7.0
# Copyright Francesco Di Blasi 2026 - All Rights Reserved
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$GodotExe,
    [ValidateSet('template_debug', 'template_release')][string[]]$Target = @('template_debug', 'template_release'),
    [string]$WorkDirectory = [IO.Path]::GetTempPath(),
    [string]$BinaryDirectory,
    [switch]$TestEosRuntime
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$engine = (Resolve-Path -LiteralPath $GodotExe).ProviderPath
$work = (Resolve-Path -LiteralPath $WorkDirectory).ProviderPath
$binarySource = if ($BinaryDirectory) {
    (Resolve-Path -LiteralPath $BinaryDirectory).ProviderPath
} else {
    Join-Path $root 'project/addons/crimson-framework/bin'
}

# Start an isolated headless Godot process and capture both output streams.
function Start-TestGodot {
    param([string]$Project, [string]$Data, [string[]]$Arguments)

    New-Item -ItemType Directory -Path $Data -Force | Out-Null
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $engine
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.EnvironmentVariables['APPDATA'] = $Data
    $start.EnvironmentVariables['LOCALAPPDATA'] = $Data
    foreach ($argument in @('--headless', '--path', $Project) + $Arguments) {
        $start.ArgumentList.Add($argument)
    }
    return [Diagnostics.Process]::Start($start)
}

# End an owned test process after completion or timeout.
function Stop-TestGodot {
    param([Diagnostics.Process]$Process)

    if ($null -ne $Process) {
        if (!$Process.HasExited) { $Process.Kill($true) }
        $Process.Dispose()
    }
}

# Require a clean exit, error-free logs, and the expected pass marker.
function Assert-TestResult {
    param(
        [Diagnostics.Process]$Process,
        [Threading.Tasks.Task[string]]$Stdout,
        [Threading.Tasks.Task[string]]$Stderr,
        [string]$Log,
        [string]$Marker,
        [string[]]$Prefix = @()
    )

    if (!$Process.WaitForExit(45000) -or !$Stdout.Wait(3000) -or !$Stderr.Wait(3000)) {
        throw "Godot test timed out. Log location: $Log"
    }
    $output = ($Prefix + $Stdout.Result + $Stderr.Result) -join [Environment]::NewLine
    [IO.File]::WriteAllText($Log, $output)
    if ($Process.ExitCode -ne 0 -or $output -match '(?m)^\s*(SCRIPT ERROR|ERROR):') {
        throw "Godot test failed (exit $($Process.ExitCode)). See $Log."
    }
    if ($Marker -and $Marker -notin ($output -split '\r?\n')) {
        throw "Godot test did not emit $Marker. See $Log."
    }
    if ($Marker) { Write-Host $Marker }
}

# Run a single-process suite with isolated application data.
function Invoke-TestGodot {
    param([string]$Project, [string]$Area, [string]$Name, [string[]]$Arguments, [string]$Marker = '')

    $process = Start-TestGodot -Project $Project -Data (Join-Path $Area "$Name-data") -Arguments $Arguments
    try {
        Assert-TestResult -Process $process -Stdout $process.StandardOutput.ReadToEndAsync() `
            -Stderr $process.StandardError.ReadToEndAsync() -Log (Join-Path $Area "$Name.log") -Marker $Marker
    } finally {
        Stop-TestGodot $process
    }
}

# Start an ENet host, wait for its readiness marker, and run the matching client.
function Invoke-TwoProcessTest {
    param(
        [string]$Project,
        [string]$Area,
        [string]$Name,
        [string]$Script,
        [string]$Ready,
        [string]$HostPassed,
        [string]$ClientPassed
    )

    $socket = [Net.Sockets.UdpClient]::new([Net.IPEndPoint]::new([Net.IPAddress]::Loopback, 0))
    try { $port = [string]$socket.Client.LocalEndPoint.Port } finally { $socket.Dispose() }
    $networkArguments = @('--script', $Script, '--')
    $server = $null
    $client = $null
    try {
        $server = Start-TestGodot -Project $Project -Data (Join-Path $Area "$Name-server-data") -Arguments ($networkArguments + @('host', $port))
        $serverErrors = $server.StandardError.ReadToEndAsync()
        $prefix = [Collections.Generic.List[string]]::new()
        $deadline = [Diagnostics.Stopwatch]::StartNew()
        while ($true) {
            $line = $server.StandardOutput.ReadLineAsync()
            $remaining = 12000 - [int]$deadline.ElapsedMilliseconds
            if ($remaining -le 0 -or !$line.Wait($remaining) -or $null -eq $line.Result) {
                $early = Join-Path $Area "$Name-server-start.log"
                $errors = if ($serverErrors.IsCompletedSuccessfully) { $serverErrors.Result } else { '' }
                [IO.File]::WriteAllText($early, (($prefix.ToArray() + $errors) -join [Environment]::NewLine))
                throw "The host did not start listening. See $early."
            }
            $prefix.Add($line.Result)
            if ($line.Result -eq $Ready) { break }
        }
        $serverOutput = $server.StandardOutput.ReadToEndAsync()
        $client = Start-TestGodot -Project $Project -Data (Join-Path $Area "$Name-client-data") -Arguments ($networkArguments + @('client', $port))
        $clientOutput = $client.StandardOutput.ReadToEndAsync()
        $clientErrors = $client.StandardError.ReadToEndAsync()
        Assert-TestResult -Process $server -Stdout $serverOutput -Stderr $serverErrors -Prefix $prefix.ToArray() `
            -Log (Join-Path $Area "$Name-server.log") -Marker $HostPassed
        Assert-TestResult -Process $client -Stdout $clientOutput -Stderr $clientErrors `
            -Log (Join-Path $Area "$Name-client.log") -Marker $ClientPassed
    } finally {
        Stop-TestGodot $client
        Stop-TestGodot $server
    }
}

# Exercise the selected ENet topology in independent Godot processes.
function Invoke-DemoSessionTest {
    param([string]$Project, [string]$Area, [switch]$Dedicated, [switch]$Online)

    $socket = [Net.Sockets.UdpClient]::new([Net.IPEndPoint]::new([Net.IPAddress]::Loopback, 0))
    try { $port = [string]$socket.Client.LocalEndPoint.Port } finally { $socket.Dispose() }
    $topology = if ($Online) { 'online' } elseif ($Dedicated) { 'dedicated' } else { 'listen' }
    $markers = (New-Item -ItemType Directory -Path (Join-Path $Area "demo-session-$topology-markers")).FullName
    $script = if ($Online) { 'res://demo/tests/online_smoke.gd' } else { 'res://demo/tests/session_smoke.gd' }
    $arguments = @('--script', $script, '--')
    if (!$Dedicated -and !$Online) {
        Invoke-TestGodot -Project $Project -Area $Area -Name 'demo-session-solo' `
            -Arguments ($arguments + @('solo', $port, $markers)) -Marker 'CRIMSON_DEMO_SESSION_PASSED solo'
    }
    $roles = if ($Online) { @('host', 'denied', 'client') }
        elseif ($Dedicated) { @('dedicated', 'dedicated-client') }
        else { @('host', 'first', 'second', 'overflow') }
    $passMarker = if ($Online) { 'CRIMSON_DEMO_ONLINE_PASSED' } else { 'CRIMSON_DEMO_SESSION_PASSED' }
    if ($Online) {
        [IO.File]::WriteAllBytes((Join-Path $markers 'fixture-key'), [Security.Cryptography.RandomNumberGenerator]::GetBytes(32))
    }

    $runs = @()
    try {
        foreach ($role in $roles) {
            $process = Start-TestGodot -Project $Project -Data (Join-Path $Area "demo-session-$topology-$role-data") `
                -Arguments ($arguments + @($role, $port, $markers))
            $runs += @{
                Role = $role
                Process = $process
                Stdout = $process.StandardOutput.ReadToEndAsync()
                Stderr = $process.StandardError.ReadToEndAsync()
            }
        }
        foreach ($run in $runs) {
            Assert-TestResult -Process $run.Process -Stdout $run.Stdout -Stderr $run.Stderr `
                -Log (Join-Path $Area "demo-session-$topology-$($run.Role).log") -Marker "$passMarker $($run.Role)"
        }
    } finally {
        foreach ($run in $runs) { Stop-TestGodot $run.Process }
    }
}

foreach ($variant in $Target) {
    $area = Join-Path $work ("crimson-multiplayer-$variant-" + [guid]::NewGuid().ToString('N'))
    $project = Join-Path $area 'project'
    $addon = Join-Path $project 'addons/crimson-framework'
    $binary = "crimson_framework.windows.$variant.x86_64.dll"
    $sourceAddon = Join-Path $root 'project/addons/crimson-framework'
    if (!(Test-Path -LiteralPath (Join-Path $binarySource $binary))) {
        throw "Build $variant before running the multiplayer tests."
    }
    New-Item -ItemType Directory -Path (Join-Path $addon 'bin') -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $binarySource $binary) -Destination (Join-Path $addon 'bin')
    $descriptor = Get-Content -Raw -LiteralPath (Join-Path $sourceAddon 'crimson-framework.gdextension')
    if ($variant -eq 'template_release') { $descriptor = $descriptor.Replace('.template_debug.', '.template_release.') }
    Set-Content -Encoding utf8NoBOM -LiteralPath (Join-Path $addon 'crimson-framework.gdextension') -Value $descriptor
    # Preserve the real demo entry point so the session fixture exercises its authored hub.
    Copy-Item -LiteralPath (Join-Path $root 'project/project.godot') -Destination $project
    $testDestination = Join-Path $project 'tests'
    New-Item -ItemType Directory -Path $testDestination | Out-Null
    foreach ($entry in @('session', 'scene_load', 'match_runtime', 'replication', 'game_flow', 'online', 'multiplayer_peer.gd')) {
        Copy-Item -LiteralPath (Join-Path $root "project/tests/$entry") -Destination $testDestination -Recurse
    }
    Copy-Item -LiteralPath (Join-Path $root 'project/demo') -Destination $project -Recurse

    Write-Host "Testing $variant. Logs and isolated data: $area"
    Invoke-TestGodot -Project $project -Area $area -Name 'import' -Arguments @('--editor', '--import', '--frame-delay', '1000')
    Invoke-TestGodot -Project $project -Area $area -Name 'session' -Arguments @('--script', 'res://tests/session/test_game_session.gd') -Marker 'CRIMSON_GAME_SESSION_TESTS_PASSED'
    Invoke-TestGodot -Project $project -Area $area -Name 'scene-load' -Arguments @('--script', 'res://tests/scene_load/scene_load_tests.gd') -Marker 'CRIMSON_SCENE_LOAD_TESTS_PASSED'
    Invoke-TestGodot -Project $project -Area $area -Name 'match-runtime' -Arguments @('--script', 'res://tests/match_runtime/test_match_runtime.gd') -Marker 'CRIMSON_MATCH_RUNTIME_TESTS_PASSED'
    Invoke-TestGodot -Project $project -Area $area -Name 'game-flow-offline' -Arguments @('--script', 'res://tests/game_flow/offline.gd') -Marker 'CRIMSON_GAME_FLOW_OFFLINE_PASSED'
    Invoke-TwoProcessTest -Project $project -Area $area -Name 'world-transition' -Script 'res://tests/multiplayer_peer.gd' `
        -Ready 'CRIMSON_MULTIPLAYER_HOST_LISTENING' -HostPassed 'CRIMSON_MULTIPLAYER_HOST_PASSED' -ClientPassed 'CRIMSON_MULTIPLAYER_CLIENT_PASSED'
    Invoke-TwoProcessTest -Project $project -Area $area -Name 'match-runtime' -Script 'res://tests/match_runtime/network_peer.gd' `
        -Ready 'CRIMSON_MATCH_HOST_READY' -HostPassed 'CRIMSON_MATCH_NETWORK_PASSED' -ClientPassed 'CRIMSON_MATCH_NETWORK_PASSED'
    Invoke-TwoProcessTest -Project $project -Area $area -Name 'replication' -Script 'res://tests/replication/network_peer.gd' `
        -Ready 'CRIMSON_REPLICATION_HOST_READY' -HostPassed 'CRIMSON_REPLICATION_NETWORK_PASSED' -ClientPassed 'CRIMSON_REPLICATION_NETWORK_PASSED'
    Invoke-TwoProcessTest -Project $project -Area $area -Name 'game-flow-network' -Script 'res://tests/game_flow/network_peer.gd' `
        -Ready 'CRIMSON_GAME_FLOW_HOST_READY' -HostPassed 'CRIMSON_GAME_FLOW_NETWORK_PASSED' -ClientPassed 'CRIMSON_GAME_FLOW_NETWORK_PASSED'
    Invoke-TestGodot -Project $project -Area $area -Name 'demo-menu' `
        -Arguments @('--script', 'res://demo/ui/tests/menu_smoke.gd') -Marker 'CRIMSON_MENU_PASS'
    Invoke-DemoSessionTest -Project $project -Area $area
    Invoke-DemoSessionTest -Project $project -Area $area -Dedicated
    Invoke-DemoSessionTest -Project $project -Area $area -Online
    if ($TestEosRuntime) {
        $runtime = Join-Path $binarySource 'EOSSDK-Win64-Shipping.dll'
        if (!(Test-Path -LiteralPath $runtime -PathType Leaf)) {
            throw 'Build with the EOS C SDK before running -TestEosRuntime.'
        }
        Invoke-TestGodot -Project $project -Area $area -Name 'eos-missing' `
            -Arguments @('--script', 'res://tests/online/eos_runtime.gd', '--', 'missing') `
            -Marker 'CRIMSON_EOS_RUNTIME_PASSED missing'
        Copy-Item -LiteralPath $runtime -Destination (Join-Path $addon 'bin')
        Invoke-TestGodot -Project $project -Area $area -Name 'eos-available' `
            -Arguments @('--script', 'res://tests/online/eos_runtime.gd', '--', 'available') `
            -Marker 'CRIMSON_EOS_RUNTIME_PASSED available'
    }
}
