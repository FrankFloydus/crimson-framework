#Requires -Version 7.0
# Copyright Francesco Di Blasi 2026 - All Rights Reserved
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$GodotExe,
    [ValidateSet('template_debug', 'template_release')][string[]]$Target = @('template_debug', 'template_release'),
    [string]$WorkDirectory = [IO.Path]::GetTempPath(),
    [string]$BinaryDirectory,
    [switch]$CoreOnly,
    [switch]$DemoLaunchSmoke
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
    $start.EnvironmentVariables['CRIMSON_NETWORK_PROFILE_STORE'] = Join-Path (Split-Path -Parent $Data) 'network-profiles'
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

# Require the expected exit status and marker, with no engine or script errors.
function Assert-TestResult {
    param(
        [Diagnostics.Process]$Process,
        [Threading.Tasks.Task[string]]$Stdout,
        [Threading.Tasks.Task[string]]$Stderr,
        [string]$Log,
        [string]$Marker,
        [string[]]$Prefix = @(),
        [int]$ExpectedExit = 0
    )

    if (!$Process.WaitForExit(45000) -or !$Stdout.Wait(3000) -or !$Stderr.Wait(3000)) {
        throw "Godot test timed out. Log location: $Log"
    }
    $output = ($Prefix + $Stdout.Result + $Stderr.Result) -join [Environment]::NewLine
    [IO.File]::WriteAllText($Log, $output)
    if ($Process.ExitCode -ne $ExpectedExit -or $output -match '(?m)^\s*(SCRIPT ERROR|ERROR):') {
        throw "Godot test failed (exit $($Process.ExitCode)). See $Log."
    }
    if ($Marker -and $Marker -notin ($output -split '\r?\n')) {
        throw "Godot test did not emit $Marker. See $Log."
    }
    if ($Marker) { Write-Host $Marker }
}

# Run a single-process suite with isolated application data.
function Invoke-TestGodot {
    param([string]$Project, [string]$Area, [string]$Name, [string[]]$Arguments, [string]$Marker = '', [int]$ExpectedExit = 0)

    $process = Start-TestGodot -Project $Project -Data (Join-Path $Area "$Name-data") -Arguments $Arguments
    try {
        Assert-TestResult -Process $process -Stdout $process.StandardOutput.ReadToEndAsync() `
            -Stderr $process.StandardError.ReadToEndAsync() -Log (Join-Path $Area "$Name.log") -Marker $Marker `
            -ExpectedExit $ExpectedExit
    } finally {
        Stop-TestGodot $process
    }
}

# Verify the launcher's actual server scene, its port override, and its failure marker.
function Invoke-DedicatedLaunchTest {
    param([string]$Project, [string]$Area)

    $socket = [Net.Sockets.UdpClient]::new([Net.IPEndPoint]::new([Net.IPAddress]::Loopback, 0))
    try { $port = $socket.Client.LocalEndPoint.Port } finally { $socket.Dispose() }
    $server = @('res://server/main.tscn', '--', '--crimson-role=server', '--crimson-bind=127.0.0.1')
    Invoke-TestGodot -Project $Project -Area $Area -Name 'dedicated-launch' `
        -Arguments (@('--quit-after', '300', '--frame-delay', '10') + $server + "--crimson-port=$port") `
        -Marker "[Crimson] READY server $port"
    Invoke-TestGodot -Project $Project -Area $Area -Name 'dedicated-launch-invalid-port' `
        -Arguments ($server + '--crimson-port=65536') -ExpectedExit 1 `
        -Marker '[Crimson] ERROR server The server port must be an integer from 1 to 65535.'
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

# Run replicated movement in separate processes with the fixture's readiness markers.
function Invoke-DemoMovementTest {
    param(
        [string]$Project,
        [string]$Area,
        [ValidateSet('listen', 'dedicated')][string]$Topology,
        [ValidateSet('', 'conditioned', 'long-outage', 'authority-opt-out', 'guest-opt-out', 'incompatible-shape', 'incompatible-tuning', 'incompatible-style', 'authored-components', 'jump', 'jump-live', 'free-orbit', 'free-orbit-live', 'forward-only', 'lock-on')][string]$Mode = ''
    )

    $socket = [Net.Sockets.UdpClient]::new([Net.IPEndPoint]::new([Net.IPAddress]::Loopback, 0))
    $port = [string]$socket.Client.LocalEndPoint.Port
    $name = if ($Mode) { "demo-movement-$Topology-$Mode" } else { "demo-movement-$Topology" }
    $arguments = @('--script', 'res://demo/tests/movement_network.gd', '--')
    $fixtureMode = @()
    if ($Mode) { $fixtureMode = @($Mode) }
    $runs = @()
    $relay = $null
    $relayLog = Join-Path $Area "$name-relay.log"
    try {
        $markers = (New-Item -ItemType Directory -Path (Join-Path $Area "$name-markers")).FullName
        $guestPort = $port
        if ($Mode -in @('conditioned', 'long-outage', 'authority-opt-out', 'guest-opt-out', 'jump', 'lock-on')) {
            $relayArguments = @('--script', 'res://demo/tests/movement_conditioner.gd', '--', $port, $markers, $Mode)
            $relay = Start-TestGodot -Project $Project -Data (Join-Path $Area "$name-relay-data") -Arguments $relayArguments
            $relayOut = $relay.StandardOutput.ReadToEndAsync()
            $relayErr = $relay.StandardError.ReadToEndAsync()
            $ready = Join-Path $markers 'relay-ready'
            $deadline = [Diagnostics.Stopwatch]::StartNew()
            while ($true) {
                if ($relay.HasExited) {
                    Assert-TestResult -Process $relay -Stdout $relayOut -Stderr $relayErr -Log $relayLog
                    throw "The movement conditioner exited before binding its port. See $relayLog."
                }
                try {
                    $guestPort = [IO.File]::ReadAllText($ready)
                    break
                } catch [IO.FileNotFoundException] {
                    # The relay has not published its readiness marker yet.
                } catch [IO.IOException] {
                    # Windows can publish the rename before another process can open the file.
                    if (($_.Exception.HResult -band 0xffff) -notin @(32, 33)) { throw }
                }
                if ($deadline.ElapsedMilliseconds -gt 10000) { throw "The movement conditioner did not publish a readable port at $ready." }
                Start-Sleep -Milliseconds 10
            }
            if ([int]$guestPort -lt 1 -or [int]$guestPort -gt 65535) {
                throw "The movement conditioner reported invalid port $guestPort."
            }
        }
        $socket.Dispose()
        $roles = @($Topology, 'guest')
        if ($Mode -notin @('incompatible-shape', 'incompatible-tuning', 'incompatible-style', 'authored-components')) { $roles += 'late' }
        foreach ($role in $roles) {
            $rolePort = if ($role -eq 'guest') { $guestPort } else { $port }
            $process = Start-TestGodot -Project $Project -Data (Join-Path $Area "$name-$role-data") `
                -Arguments ($arguments + @($role, $rolePort, $markers) + $fixtureMode)
            $runs += @{
                Role = $role
                Process = $process
                Stdout = $process.StandardOutput.ReadToEndAsync()
                Stderr = $process.StandardError.ReadToEndAsync()
            }
        }
        $failures = @()
        foreach ($run in $runs) {
            try {
                Assert-TestResult -Process $run.Process -Stdout $run.Stdout -Stderr $run.Stderr `
                    -Log (Join-Path $Area "$name-$($run.Role).log") -Marker "CRIMSON_DEMO_MOVEMENT_PASSED $($run.Role)"
            } catch {
                $failures += $_.Exception.Message
            }
        }
        if ($null -ne $relay) {
            [IO.File]::WriteAllText((Join-Path $markers 'relay-stop'), '')
            try {
                Assert-TestResult -Process $relay -Stdout $relayOut -Stderr $relayErr -Log $relayLog `
                    -Marker 'CRIMSON_MOVEMENT_CONDITIONER_PASSED'
            } catch {
                $failures += $_.Exception.Message
            }
        }
        if ($failures.Count -gt 0) { throw ($failures -join [Environment]::NewLine) }
    } finally {
        $socket.Dispose()
        foreach ($run in $runs) { Stop-TestGodot $run.Process }
        Stop-TestGodot $relay
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
    $fixtureDescriptor = [regex]::Replace($descriptor, '(?ms)^\[icons\].*?(?=^\[|\z)', '')
    Set-Content -Encoding utf8NoBOM -LiteralPath (Join-Path $addon 'crimson-framework.gdextension') -Value $fixtureDescriptor
    @'
config_version=5

[application]

config/name="Crimson Module Fixtures"

[physics]

common/physics_ticks_per_second=60
common/physics_jitter_fix=0.0
common/physics_interpolation=true
'@ | Set-Content -Encoding utf8NoBOM -LiteralPath (Join-Path $project 'project.godot')
    $testDestination = Join-Path $project 'tests'
    New-Item -ItemType Directory -Path $testDestination | Out-Null
    foreach ($entry in @('fixtures', 'session', 'scene_load', 'match_runtime', 'replication', 'game_flow', 'player_controls', 'input', 'multiplayer_peer.gd')) {
        Copy-Item -LiteralPath (Join-Path $root "project/tests/$entry") -Destination $testDestination -Recurse
    }

    Write-Host "Testing $variant. Logs and isolated data: $area"
    $godotMetadata = Join-Path $project '.godot'
    New-Item -ItemType Directory -Path $godotMetadata | Out-Null
    Set-Content -LiteralPath (Join-Path $godotMetadata 'extension_list.cfg') -Encoding utf8NoBOM -Value 'res://addons/crimson-framework/crimson-framework.gdextension'
    Invoke-TestGodot -Project $project -Area $area -Name 'session' -Arguments @('--script', 'res://tests/session/test_game_session.gd') -Marker 'CRIMSON_GAME_SESSION_TESTS_PASSED'
    Invoke-TestGodot -Project $project -Area $area -Name 'network-codec' -Arguments @('--script', 'res://tests/replication/network_codec.gd') -Marker 'CRIMSON_NETWORK_CODEC_TESTS_PASSED'
    Invoke-TestGodot -Project $project -Area $area -Name 'movement-scale' `
        -Arguments @('--script', 'res://tests/replication/movement_scale.gd', '--', '32') -Marker 'CRIMSON_MOVEMENT_SCALE_PASSED'
    Invoke-TestGodot -Project $project -Area $area -Name 'scene-load' -Arguments @('--script', 'res://tests/scene_load/scene_load_tests.gd') -Marker 'CRIMSON_SCENE_LOAD_TESTS_PASSED'
    Invoke-TestGodot -Project $project -Area $area -Name 'controller-styles' -Arguments @('--script', 'res://tests/player_controls/controller_styles.gd') -Marker 'CRIMSON_CONTROLLER_STYLES_TESTS_PASSED'
    Invoke-TestGodot -Project $project -Area $area -Name 'action-maps' -Arguments @('--script', 'res://tests/player_controls/action_maps.gd') -Marker 'CRIMSON_ACTION_MAPS_TESTS_PASSED'
    Invoke-TestGodot -Project $project -Area $area -Name 'input-asset-migration' -Arguments @('--script', 'res://tests/input/asset_migration.gd') -Marker 'CRIMSON_INPUT_ASSET_MIGRATION_PASSED'
    Invoke-TestGodot -Project $project -Area $area -Name 'match-runtime' -Arguments @('--script', 'res://tests/match_runtime/test_match_runtime.gd') -Marker 'CRIMSON_MATCH_RUNTIME_TESTS_PASSED'
    Invoke-TestGodot -Project $project -Area $area -Name 'game-flow-offline' -Arguments @('--script', 'res://tests/game_flow/offline.gd') -Marker 'CRIMSON_GAME_FLOW_OFFLINE_PASSED'
    Invoke-TestGodot -Project $project -Area $area -Name 'game-code-first' -Arguments @('--script', 'res://tests/game_flow/code_first.gd') -Marker 'CRIMSON_CODE_FIRST_PASSED'
    $offlineLog = Join-Path $area 'game-flow-offline.log'
    $restoreDiagnostic = @(Select-String -LiteralPath $offlineLog -Pattern '^\[Crimson\].*RestoreFailed.*Fixture score is blocked for restore\.$')
    if ($restoreDiagnostic.Count -ne 1) {
        throw "Expected one Crimson diagnostic with the restore validator's reason. See $offlineLog."
    }
    Invoke-TwoProcessTest -Project $project -Area $area -Name 'world-transition' -Script 'res://tests/multiplayer_peer.gd' `
        -Ready 'CRIMSON_MULTIPLAYER_HOST_LISTENING' -HostPassed 'CRIMSON_MULTIPLAYER_HOST_PASSED' -ClientPassed 'CRIMSON_MULTIPLAYER_CLIENT_PASSED'
    Invoke-TwoProcessTest -Project $project -Area $area -Name 'match-runtime' -Script 'res://tests/match_runtime/network_peer.gd' `
        -Ready 'CRIMSON_MATCH_HOST_READY' -HostPassed 'CRIMSON_MATCH_NETWORK_PASSED' -ClientPassed 'CRIMSON_MATCH_NETWORK_PASSED'
    Invoke-TwoProcessTest -Project $project -Area $area -Name 'inventory-network' -Script 'res://tests/match_runtime/inventory_network.gd' `
        -Ready 'CRIMSON_INVENTORY_HOST_READY' -HostPassed 'CRIMSON_INVENTORY_NETWORK_PASSED' -ClientPassed 'CRIMSON_INVENTORY_NETWORK_PASSED'
    Invoke-TwoProcessTest -Project $project -Area $area -Name 'replication' -Script 'res://tests/replication/network_peer.gd' `
        -Ready 'CRIMSON_REPLICATION_HOST_READY' -HostPassed 'CRIMSON_REPLICATION_NETWORK_PASSED' -ClientPassed 'CRIMSON_REPLICATION_NETWORK_PASSED'
    Invoke-TwoProcessTest -Project $project -Area $area -Name 'game-flow-network' -Script 'res://tests/game_flow/network_peer.gd' `
        -Ready 'CRIMSON_GAME_FLOW_HOST_READY' -HostPassed 'CRIMSON_GAME_FLOW_NETWORK_PASSED' -ClientPassed 'CRIMSON_GAME_FLOW_NETWORK_PASSED'
    if ($CoreOnly) { continue }

    # Stage only the gameplay tools and their test-owned scenes after the module checks.
    $demoSource = Join-Path $root 'project/demo'
    $demoStage = Join-Path $project 'demo'
    New-Item -ItemType Directory -Path $demoStage | Out-Null
    foreach ($folder in @('tests', 'session', 'ui')) {
        $stageFolder = Join-Path $demoStage $folder
        New-Item -ItemType Directory -Path $stageFolder | Out-Null
        Copy-Item -Path (Join-Path $demoSource "$folder/*.gd") -Destination $stageFolder
    }
    $uiTestStage = Join-Path $demoStage 'ui/tests'
    New-Item -ItemType Directory -Path $uiTestStage | Out-Null
    Copy-Item -Path (Join-Path $demoSource 'ui/tests/*.gd') -Destination $uiTestStage
    Invoke-TestGodot -Project $project -Area $area -Name 'demo-menu' `
        -Arguments @('--script', 'res://demo/ui/tests/menu_smoke.gd') -Marker 'CRIMSON_MENU_PASS'
    Invoke-TestGodot -Project $project -Area $area -Name 'network-debugger-ui' `
        -Arguments @('--script', 'res://demo/ui/tests/network_simulator_ui.gd') -Marker 'CRIMSON_NETWORK_SIMULATOR_UI_PASS guest'
    Invoke-TestGodot -Project $project -Area $area -Name 'network-debugger-ui-reopen' `
        -Arguments @('--script', 'res://demo/ui/tests/network_simulator_ui.gd', '--', '--verify-profile') `
        -Marker 'CRIMSON_NETWORK_SIMULATOR_UI_PASS profile'
    Invoke-TestGodot -Project $project -Area $area -Name 'network-pacing' `
        -Arguments @('--script', 'res://demo/tests/network_pacing.gd') -Marker 'CRIMSON_NETWORK_PACING_PASS'
    Invoke-TwoProcessTest -Project $project -Area $area -Name 'network-debugger' -Script 'res://demo/tests/network_debugger.gd' `
        -Ready 'CRIMSON_NETWORK_DEBUGGER_READY' -HostPassed 'CRIMSON_NETWORK_DEBUGGER_PASS host' `
        -ClientPassed 'CRIMSON_NETWORK_DEBUGGER_PASS client'
    Invoke-DemoSessionTest -Project $project -Area $area
    Invoke-DemoSessionTest -Project $project -Area $area -Dedicated
    Invoke-DemoMovementTest -Project $project -Area $area -Topology listen
    Invoke-DemoMovementTest -Project $project -Area $area -Topology dedicated
    Invoke-DemoMovementTest -Project $project -Area $area -Topology listen -Mode conditioned
    Invoke-DemoMovementTest -Project $project -Area $area -Topology dedicated -Mode conditioned
    Invoke-DemoMovementTest -Project $project -Area $area -Topology listen -Mode long-outage
    Invoke-DemoMovementTest -Project $project -Area $area -Topology listen -Mode authority-opt-out
    Invoke-DemoMovementTest -Project $project -Area $area -Topology listen -Mode guest-opt-out
    Invoke-DemoMovementTest -Project $project -Area $area -Topology listen -Mode incompatible-shape
    Invoke-DemoMovementTest -Project $project -Area $area -Topology listen -Mode incompatible-tuning
    Invoke-DemoMovementTest -Project $project -Area $area -Topology listen -Mode authored-components
    Invoke-DemoMovementTest -Project $project -Area $area -Topology dedicated -Mode jump
    Invoke-DemoMovementTest -Project $project -Area $area -Topology listen -Mode jump-live
    Invoke-DemoMovementTest -Project $project -Area $area -Topology listen -Mode free-orbit
    Invoke-DemoMovementTest -Project $project -Area $area -Topology listen -Mode free-orbit-live
    Invoke-DemoMovementTest -Project $project -Area $area -Topology listen -Mode forward-only
    Invoke-DemoMovementTest -Project $project -Area $area -Topology dedicated -Mode lock-on
    Invoke-DemoMovementTest -Project $project -Area $area -Topology listen -Mode incompatible-style
    if ($DemoLaunchSmoke) {
        $demoProject = Join-Path $area 'demo-project'
        New-Item -ItemType Directory -Path $demoProject | Out-Null
        Copy-Item -LiteralPath (Join-Path $project 'addons') -Destination $demoProject -Recurse
        $demoAddon = Join-Path $demoProject 'addons/crimson-framework'
        foreach ($entry in @('images', 'input')) {
            $assetDirectory = Join-Path $sourceAddon $entry
            if (Test-Path -LiteralPath $assetDirectory) {
                Copy-Item -LiteralPath $assetDirectory -Destination $demoAddon -Recurse
            }
        }
        Set-Content -Encoding utf8NoBOM -LiteralPath (Join-Path $demoAddon 'crimson-framework.gdextension') -Value $descriptor
        Copy-Item -LiteralPath $demoSource -Destination $demoProject -Recurse
        foreach ($folder in @('demo/tests', 'demo/ui/tests')) {
            $testFolder = Join-Path $demoProject $folder
            if (Test-Path -LiteralPath $testFolder) {
                New-Item -ItemType File -Path (Join-Path $testFolder '.gdignore') | Out-Null
            }
        }
        Copy-Item -LiteralPath (Join-Path $root 'project/project.godot') -Destination $demoProject
        Copy-Item -LiteralPath (Join-Path (Split-Path -Parent $root) 'server') -Destination $demoProject -Recurse
        Invoke-TestGodot -Project $demoProject -Area $area -Name 'demo-import' -Arguments @('--editor', '--import', '--frame-delay', '1000')
        Invoke-DedicatedLaunchTest -Project $demoProject -Area $area
    }
    Invoke-DemoSessionTest -Project $project -Area $area -Online
}
