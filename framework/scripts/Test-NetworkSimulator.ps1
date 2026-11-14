#Requires -Version 7.0
# Copyright Francesco Di Blasi 2026 - All Rights Reserved
<#
.SYNOPSIS
Runs the demo's headless network simulator tests against owned loopback hosts.
.DESCRIPTION
Quick checks UDP pacing and the CLI against a listen host. Full also checks a
dedicated host, error exits and real ENet timeout recovery. Stress repeats the
dedicated plans with concurrent guests while retaining every process trace.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$GodotExe,
    [ValidateSet('Quick', 'Full', 'Stress')][string]$Suite = 'Quick',
    [ValidateSet('template_debug', 'template_release')][string[]]$Target = @('template_debug'),
    [string]$WorkDirectory = [IO.Path]::GetTempPath(),
    [string]$BinaryDirectory,
    [ValidateRange(2, 10)][int]$Repeat = 3
)

$ErrorActionPreference = 'Stop'
$framework = Split-Path -Parent $PSScriptRoot
$source = Join-Path $framework 'project'
$engine = (Resolve-Path -LiteralPath $GodotExe).ProviderPath
$work = (Resolve-Path -LiteralPath $WorkDirectory).ProviderPath
$binarySource = if ($BinaryDirectory) {
    (Resolve-Path -LiteralPath $BinaryDirectory).ProviderPath
} else {
    Join-Path $source 'addons/crimson-framework/bin'
}
$loopback = [Net.IPAddress]::Loopback
$processTimeoutMs = 45000
$readyTimeoutMs = 12000
$cliScript = 'res://tests/fixtures/network_simulator_cli_fixture.gd'
$fixtureScript = 'res://demo/tests/network_debugger.gd'
$suiteFolder = Join-Path $work ("crimson-network-simulator-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $suiteFolder | Out-Null
Write-Host "Network Simulator $Suite logs and JSONL traces: $suiteFolder"

# Stage a matching addon and current demo without importing assets into the working project.
function Initialize-TestProject {
    param([string]$Variant, [string]$Area)
    $staged = Join-Path $Area 'project'
    $addon = Join-Path $staged 'addons/crimson-framework'
    $originalAddon = Join-Path $source 'addons/crimson-framework'
    $binary = "crimson_framework.windows.$Variant.x86_64.dll"
    $binaryPath = Join-Path $binarySource $binary
    if (!(Test-Path -LiteralPath $binaryPath -PathType Leaf)) {
        throw "Build $Variant before running the simulator tests. Missing $binaryPath."
    }
    New-Item -ItemType Directory -Path (Join-Path $addon 'bin') | Out-Null
    Copy-Item -LiteralPath $binaryPath -Destination (Join-Path $addon 'bin')
    $descriptor = Get-Content -Raw -LiteralPath (Join-Path $originalAddon 'crimson-framework.gdextension')
    if ($Variant -eq 'template_release') { $descriptor = $descriptor.Replace('.template_debug.', '.template_release.') }
    $descriptor = [regex]::Replace($descriptor, '(?ms)^\[icons\].*?(?=^\[|\z)', '')
    Set-Content -LiteralPath (Join-Path $addon 'crimson-framework.gdextension') -Value $descriptor -Encoding utf8NoBOM
    @'
config_version=5

[application]

config/name="Crimson Network Simulator Fixtures"

[physics]

common/physics_ticks_per_second=60
'@ | Set-Content -Encoding utf8NoBOM -LiteralPath (Join-Path $staged 'project.godot')
    $fixtures = Join-Path $staged 'tests'
    New-Item -ItemType Directory -Path $fixtures | Out-Null
    Copy-Item -LiteralPath (Join-Path $source 'tests/fixtures') -Destination $fixtures -Recurse
    foreach ($folder in @('session', 'tests')) {
        $destination = Join-Path $staged "demo/$folder"
        New-Item -ItemType Directory -Path $destination -Force | Out-Null
        Copy-Item -Path (Join-Path $source "demo/$folder/*.gd") -Destination $destination
    }
    Copy-Item -LiteralPath (Join-Path $source 'demo/tests/network_simulator_full.json') -Destination (Join-Path $staged 'demo/tests')
    $godotMetadata = Join-Path $staged '.godot'
    New-Item -ItemType Directory -Path $godotMetadata | Out-Null
    Set-Content -LiteralPath (Join-Path $godotMetadata 'extension_list.cfg') -Encoding utf8NoBOM -Value 'res://addons/crimson-framework/crimson-framework.gdextension'
    return $staged
}

# Choose a local ephemeral UDP port without taking over an existing server.
function Get-TestPort {
    $socket = [Net.Sockets.UdpClient]::new([Net.IPEndPoint]::new($loopback, 0))
    try { return $socket.Client.LocalEndPoint.Port } finally { $socket.Dispose() }
}

# Run one process with its own Godot data, profile store, and captured console streams.
function Start-TestProcess {
    param([string]$Project, [string]$Area, [string]$Name, [string[]]$Arguments, [string]$Ready = '')
    $data = Join-Path $Area "$Name-data"
    New-Item -ItemType Directory -Path $data | Out-Null
    $start = [Diagnostics.ProcessStartInfo]::new($engine)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($key in @('APPDATA', 'LOCALAPPDATA', 'XDG_DATA_HOME', 'XDG_CONFIG_HOME', 'XDG_CACHE_HOME')) {
        $start.EnvironmentVariables[$key] = $data
    }
    $start.EnvironmentVariables['CRIMSON_NETWORK_PROFILE_STORE'] = Join-Path $data 'profiles'
    foreach ($argument in @('--headless', '--path', $Project) + $Arguments) { $start.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::Start($start)
    $run = @{
        Process = $process; Started = [Diagnostics.Stopwatch]::StartNew()
        Name = $Name; Log = Join-Path $Area "$Name.log"
        Prefix = [Collections.Generic.List[string]]::new()
        Stdout = $null; Stderr = $process.StandardError.ReadToEndAsync()
    }
    if (!$Ready) {
        $run.Stdout = $process.StandardOutput.ReadToEndAsync()
        return $run
    }
    while ($true) {
        $line = $process.StandardOutput.ReadLineAsync()
        $remaining = [Math]::Max(0, $readyTimeoutMs - [int]$run.Started.ElapsedMilliseconds)
        if (!$line.Wait($remaining) -or $null -eq $line.Result) { break }
        $run.Prefix.Add($line.Result)
        if ($line.Result -eq $Ready) {
            $run.Stdout = $process.StandardOutput.ReadToEndAsync()
            return $run
        }
    }
    if (!$process.HasExited) { $process.Kill($true) }
    if ($line.Wait(3000) -and $null -ne $line.Result) { $run.Prefix.Add($line.Result) }
    if ($line.IsCompleted) { $run.Stdout = $process.StandardOutput.ReadToEndAsync() }
    Stop-TestProcess $run
    throw "$Name did not start listening. See $($run.Log)."
}

# Capture both streams and clean up only processes started by this suite.
function Stop-TestProcess {
    param([hashtable]$Run)
    if ($null -eq $Run) { return }
    $process = $Run.Process
    try {
        if (!$process.HasExited) { $process.Kill($true) }
        $process.WaitForExit(3000) | Out-Null
        $lines = @($Run.Prefix)
        foreach ($stream in @($Run.Stdout, $Run.Stderr)) {
            if ($null -ne $stream -and $stream.Wait(3000)) { $lines += $stream.Result }
        }
        $Run.Output = $lines -join [Environment]::NewLine
        [IO.File]::WriteAllText($Run.Log, $Run.Output)
    } finally { $process.Dispose() }
}

# Check actual exit status and surface engine or script errors even when a marker was emitted.
function Complete-TestProcess {
    param([hashtable]$Run, [int]$ExpectedExit = 0, [string]$Marker = '')
    $remaining = [Math]::Max(0, $processTimeoutMs - [int]$Run.Started.ElapsedMilliseconds)
    $completed = $Run.Process.WaitForExit($remaining)
    $exit = if ($completed) { $Run.Process.ExitCode } else { 'timeout' }
    Stop-TestProcess $Run
    if (!$completed -or $exit -ne $ExpectedExit -or $Run.Output -match '(?m)^\s*(SCRIPT ERROR|ERROR):') {
        throw "$($Run.Name) returned $exit (expected $ExpectedExit). See $($Run.Log)."
    }
    if ($Marker -and $Marker -notin ($Run.Output -split '\r?\n')) {
        throw "$($Run.Name) did not report $Marker. See $($Run.Log)."
    }
}

# Read every JSON line and require one explicit result and the expected trace events.
function Assert-TestTrace {
    param([hashtable]$Run, [int]$ExpectedExit = 0, [string[]]$RequiredRecords = @())
    $trace = $Run.Trace
    if (!(Test-Path -LiteralPath $trace -PathType Leaf)) { throw "Missing trace $trace. See $($Run.Log)." }
    $records = @(Get-Content -LiteralPath $trace | ForEach-Object { ConvertFrom-Json -InputObject $_ -AsHashtable })
    $finished = @($records | Where-Object { $_.type -ceq 'run_finished' })
    if ($records.Count -eq 0 -or @($records | Where-Object { $_.type -isnot [string] }).Count -gt 0 `
            -or $records[-1].type -cne 'run_finished' -or $finished.Count -ne 1 `
            -or $finished[0].ok -isnot [bool] -or $finished[0].ok -ne ($ExpectedExit -eq 0) `
            -or $finished[0].exit_code -ne $ExpectedExit) {
        throw "Unexpected trace outcome in $trace. See $($Run.Log)."
    }
    foreach ($record in $RequiredRecords) {
        if ($record -cnotin $records.type) { throw "Missing $record in $trace." }
    }
    return ,$records
}

# Boot the actual CLI runner. Its trace stays separate from engine logging.
function Start-Simulator {
    param([string]$Project, [string]$Area, [string]$Name, [int]$Port, [string]$Plan)
    $trace = Join-Path $Area "$Name.jsonl"
    $run = Start-TestProcess $Project $Area $Name @('--script', $cliScript, '--', "--port=$Port", "--plan=$Plan", "--output=$trace")
    $run.Trace = $trace
    return $run
}

# Collect successful traces, failures, and console output for every started guest.
function Complete-SimulatorGuests {
    param([hashtable[]]$Runs, [bool]$VerifyMovement)
    $errors = [Collections.Generic.List[string]]::new()
    $peers = [Collections.Generic.HashSet[long]]::new()
    $slots = [Collections.Generic.HashSet[int]]::new()
    foreach ($run in $Runs) {
        try {
            Complete-TestProcess $run
            $records = Assert-TestTrace $run 0 @('run_started', 'step_started', 'snapshot', 'assertion', 'event')
            $connected = @($records | Where-Object { $_.type -eq 'event' -and $_.event -eq 'connected' })
            if ($connected.Count -ne 2) { throw "Expected an initial join and a rejoin in $($run.Trace)." }
            foreach ($connection in $connected) {
                if ($connection.peer_id -le 1 -or !$peers.Add($connection.peer_id)) {
                    throw "Expected a distinct admitted peer ID for each connection in $($run.Trace)."
                }
            }
            $leave = @($records | Where-Object { $_.type -eq 'event' -and $_.event -eq 'session_ended' -and $_.reason -eq 'left' })
            if ($leave.Count -lt 1 -or $leave[0].step -le $connected[0].step -or $leave[0].step -ge $connected[1].step) {
                throw "Expected a normal leave between the two connections in $($run.Trace)."
            }
            if ($Runs.Count -gt 1) {
                $cohort = @($records | Where-Object { $_.type -eq 'snapshot' -and $_.label -eq 'All guests mounted' })
                if ($cohort.Count -ne 1 -or $cohort[0].game.player_count -ne $Runs.Count `
                        -or $cohort[0].game.connected_peers -ne 1 -or $cohort[0].game.player_number -le 0 `
                        -or !$slots.Add($cohort[0].game.player_number)) {
                    throw "Expected all simultaneous actors, one direct authority connection, and a distinct slot in $($run.Trace)."
                }
            }
            if ($VerifyMovement) { Assert-SimulatorMovement $records $run.Trace }
            if (@($records | Where-Object { $_.type -eq 'assertion' -and $_.ok -ne $true }).Count -gt 0) {
                throw "A successful trace contains a failed assertion in $($run.Trace)."
            }
        } catch { $errors.Add($_.Exception.Message) }
    }
    if ($errors.Count -gt 0) { throw ($errors -join [Environment]::NewLine) }
}

# Verify the game consumed synthetic movement and recovered to the authoritative pose.
function Assert-SimulatorMovement {
    param([object[]]$Records, [string]$Trace)
    $start = @($Records | Where-Object { $_.type -eq 'snapshot' -and $_.label -eq 'Before movement' })
    $end = @($Records | Where-Object { $_.type -eq 'snapshot' -and $_.label -eq 'Before reconnect' })
    if ($start.Count -ne 1 -or $end.Count -ne 1 -or $null -eq $start[0].game.local_position -or $null -eq $end[0].game.local_position) {
        throw "Missing authored movement snapshots in $Trace."
    }
    $initial = $start[0].game.local_position
    $current = $end[0].game.local_position
    $authority = $end[0].game.authority_position
    if ([Math]::Abs($authority.z - $initial.z) -lt 0.75 -or [Math]::Abs($authority.z - $current.z) -gt 0.5 `
            -or [Math]::Abs($authority.x - $current.x) -gt 0.5) {
        throw "The guest did not move and converge to the authority after simulated faults. See $Trace."
    }
}

# Confirm each stress guest sees the complete authoritative roster before applying faults.
function New-StressPlan {
    param([string]$Plan, [string]$Area, [int]$Guests)
    $document = Get-Content -Raw -LiteralPath $Plan | ConvertFrom-Json -AsHashtable
    $document.steps = @(
        @{ action = 'wait_until'; field = 'game.player_count'; op = 'eq'; value = $Guests; timeout_ms = 5000 }
        @{ action = 'status'; label = 'All guests mounted' }
    ) + $document.steps
    $path = Join-Path $Area 'stress-plan.json'
    $document | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $path
    return $path
}

# Exercise actual listen and dedicated startup, then run the same CLI a developer invokes.
function Invoke-SimulatorHost {
    param([string]$Project, [string]$Area, [string]$Topology, [string]$Plan, [int]$Waves = 1, [int]$GuestCount = 1)
    $port = Get-TestPort
    $role = if ($Topology -eq 'listen') { 'listen' } else { 'server' }
    $arguments = @('--script', 'res://tests/fixtures/network_simulator_authority.gd', '--',
        "--crimson-role=$role", '--crimson-bind=127.0.0.1', "--crimson-port=$port")
    $authority = Start-TestProcess $Project $Area "$Topology-host" $arguments "[Crimson] READY $role $port"
    try {
        if ($Topology -eq 'dedicated') { Invoke-SimulatorFailures $Project $Area $port }
        for ($wave = 1; $wave -le $Waves; $wave++) {
            $runs = [Collections.Generic.List[hashtable]]::new()
            try {
                for ($guest = 1; $guest -le $GuestCount; $guest++) {
                    $runs.Add((Start-Simulator $Project $Area "$Topology-wave$wave-guest$guest" $port $Plan))
                }
            } catch {
                foreach ($run in $runs) { Stop-TestProcess $run }
                throw
            }
            Complete-SimulatorGuests $runs.ToArray() ($Topology -eq 'dedicated')
            Write-Host "$Topology wave $wave passed ($GuestCount guest(s))"
        }
    } finally { Stop-TestProcess $authority }
    if ($authority.Output -match '(?m)^\s*(SCRIPT ERROR:|ERROR:|\[Crimson\] ERROR\b)') {
        throw "$Topology host reported an error. See $($authority.Log)."
    }
}

# Test the developer-facing distinction between a rejected plan and a runtime assertion failure.
function Invoke-SimulatorFailures {
    param([string]$Project, [string]$Area, [int]$Port)
    $invalid = Join-Path $Area 'invalid-plan.json'
    Set-Content -LiteralPath $invalid -Value '{"steps":[{"action":"reconnect"},{"action":"apply_profile","preset":"bogus"}]}'
    $run = Start-Simulator $Project $Area 'invalid-plan' $Port $invalid
    Complete-TestProcess $run 2
    $records = Assert-TestTrace $run 2
    if ($records.Count -ne 1 -or $records[-1].message -notmatch 'steps\[1\]\.preset') {
        throw "The invalid plan must identify the invalid field without booting the game. See $($run.Trace)."
    }

    $assertion = Join-Path $Area 'failing-assertion.json'
    Set-Content -LiteralPath $assertion -Value '{"steps":[{"action":"assert","field":"game.role","op":"eq","value":"dedicated"}]}'
    $run = Start-Simulator $Project $Area 'failing-assertion' $Port $assertion
    Complete-TestProcess $run 1
    $records = Assert-TestTrace $run 1 @('step_started', 'assertion')
    $failed = @($records | Where-Object { $_.type -eq 'assertion' -and $_.ok -eq $false })
    if ($failed.Count -ne 1 -or $failed[0].actual -ne 'guest' -or $failed[0].expected -ne 'dedicated') {
        throw "The failed assertion must identify the expected and observed roles. See $($run.Trace)."
    }
}

# Retain the live ENet regression checks for directional outage, expiry, timeout and rejoin.
function Invoke-ENetRecovery {
    param([string]$Project, [string]$Area)
    $port = Get-TestPort
    $hostRun = Start-TestProcess $Project $Area 'enet-host' @('--script', $fixtureScript, '--', 'host', "$port") 'CRIMSON_NETWORK_DEBUGGER_READY'
    try {
        $guestRun = Start-TestProcess $Project $Area 'enet-guest' @('--script', $fixtureScript, '--', 'client', "$port")
        Complete-TestProcess $guestRun 0 'CRIMSON_NETWORK_DEBUGGER_PASS client'
    } catch {
        Stop-TestProcess $hostRun
        throw
    }
    Complete-TestProcess $hostRun 0 'CRIMSON_NETWORK_DEBUGGER_PASS host'
}

foreach ($variant in $Target) {
    $area = Join-Path $suiteFolder $variant
    $project = Initialize-TestProject $variant $area
    $examplePlan = Join-Path $project 'tests/fixtures/network_simulator_quick.json'
    $fullPlan = Join-Path $project 'demo/tests/network_simulator_full.json'
    Complete-TestProcess (Start-TestProcess $project $area 'pacing' @('--script', 'res://demo/tests/network_pacing.gd')) 0 'CRIMSON_NETWORK_PACING_PASS'
    Invoke-SimulatorHost $project $area 'listen' $examplePlan
    if ($Suite -ne 'Quick') {
        $waves = if ($Suite -eq 'Stress') { $Repeat } else { 1 }
        $guests = if ($Suite -eq 'Stress') { 3 } else { 1 }
        if ($guests -gt 1) { $fullPlan = New-StressPlan $fullPlan $area $guests }
        Invoke-SimulatorHost $project $area 'dedicated' $fullPlan $waves $guests
        Invoke-ENetRecovery $project $area
    }
    Write-Host "CRIMSON_NETWORK_SIMULATOR_SUITE_PASS $Suite $variant"
}
