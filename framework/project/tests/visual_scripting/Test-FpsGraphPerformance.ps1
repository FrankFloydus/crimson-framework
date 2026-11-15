#Requires -Version 7.0
# Copyright Francesco Di Blasi 2026 - All Rights Reserved
[CmdletBinding(DefaultParameterSetName = 'Crimson')]
param(
    [Parameter(Mandatory)][string]$GodotExe,
    [Parameter(ParameterSetName = 'Crimson')][string]$BinaryDirectory,
    [Parameter(Mandatory, ParameterSetName = 'Orchestrator')][string]$OrchestratorAddon,
    [Parameter(Mandatory, ParameterSetName = 'Orchestrator')][string]$OrchestratorGraphs,
    [Parameter(ParameterSetName = 'Orchestrator')][switch]$NaturalExit,
    [string[]]$Scenario,
    [ValidateRange(16, 65536)][int]$Events = 2048,
    [ValidateRange(1, 201)][int]$Trials = 5,
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
if (!$IsWindows) { throw 'This harness uses Windows x86-64 release addons.' }
$engine = (Resolve-Path -LiteralPath $GodotExe).ProviderPath
$sourceSuite = Join-Path $PSScriptRoot 'fps_benchmark'
$sourceManifest = Join-Path $sourceSuite 'scenarios.json'
$manifest = Get-Content -Raw -LiteralPath $sourceManifest | ConvertFrom-Json
$caseIds = @($manifest.scenarios.id)
if ($manifest.version -ne 1 -or !$caseIds.Count -or
    ($caseIds | Where-Object { $_ -cnotmatch '^[a-z][a-z0-9_]*$' }) -or
    @($caseIds | Sort-Object -Unique).Count -ne $caseIds.Count) {
    throw 'The FPS benchmark manifest contains an invalid version or scenario ID.'
}
if ($Scenario.Count -gt 0) {
    $selectedIds = @($caseIds | Where-Object { $_ -cin $Scenario })
    if ($selectedIds.Count -ne $Scenario.Count) { throw 'Unknown or duplicate scenario ID.' }
} else {
    $selectedIds = $caseIds
}

$useOrchestrator = $PSCmdlet.ParameterSetName -eq 'Orchestrator'
$parkAfterResult = $useOrchestrator -and !$NaturalExit
$backend = if ($useOrchestrator) { 'orchestrator' } else { 'crimson' }
$marker = 'FPS_GRAPH_BENCHMARK_PASSED '
$runName = 'crimson-fps-graph-performance-' + [guid]::NewGuid().ToString('N')
$outputRoot = if ($OutputDirectory) {
    (New-Item -ItemType Directory -Path $OutputDirectory -Force).FullName
} else {
    [IO.Path]::GetTempPath()
}
$area = Join-Path $outputRoot $runName
$project = Join-Path $area 'project'
$data = Join-Path $area 'data'
$fixture = Join-Path $project 'tests/visual_scripting/fps_benchmark'
$scenarioPath = Join-Path $fixture 'scenarios'
$logPath = Join-Path $area 'benchmark.log'
New-Item -ItemType Directory -Path (Join-Path $project '.godot'), $data, $scenarioPath -Force | Out-Null
foreach ($file in @('fps_benchmark.gd', 'scenarios.json')) {
    Copy-Item -LiteralPath (Join-Path $sourceSuite $file) -Destination $fixture
}
& python (Join-Path $sourceSuite 'generate_fps_graphs.py') --manifest $sourceManifest --output $scenarioPath | Write-Host
if ($LASTEXITCODE -ne 0) { throw 'Could not generate the FPS benchmark fixtures.' }

# Load release DLLs with either Windows Godot selector in the isolated project.
if ($useOrchestrator) {
    $sourceAddon = (Resolve-Path -LiteralPath $OrchestratorAddon).ProviderPath
    $sourceGraphs = (Resolve-Path -LiteralPath $OrchestratorGraphs).ProviderPath
    $addon = Join-Path $project 'addons/orchestrator'
    $descriptorName = 'orchestrator.gdextension'
    $graphExtension = 'torch'
    $releaseName = 'orchestrator.windows.64.release.dll'
    $binary = (Resolve-Path -LiteralPath (Join-Path $sourceAddon $releaseName)).ProviderPath
    $descriptor = Get-Content -Raw -LiteralPath (Join-Path $sourceAddon $descriptorName)
    if ($descriptor -notmatch '(?m)^\s*windows\.x86_64\s*=\s*"res://addons/orchestrator/orchestrator\.windows\.64\.release\.dll"\s*$' -or
        $descriptor -notmatch '(?m)^\s*windows\.debug\.x86_64\s*=\s*"res://addons/orchestrator/orchestrator\.windows\.64\.(editor|release)\.dll"\s*$') {
        throw 'Pass the official Orchestrator Windows addon directory.'
    }
    $descriptor = $descriptor.Replace('orchestrator.windows.64.editor.dll', $releaseName)
    New-Item -ItemType Directory -Path $addon -Force | Out-Null
    Copy-Item -LiteralPath $binary -Destination $addon
    foreach ($scenarioId in $selectedIds) {
        Copy-Item -LiteralPath (Join-Path $sourceGraphs "$scenarioId.torch") -Destination $scenarioPath
    }
} else {
    $sourceAddon = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../../addons/crimson-framework')).ProviderPath
    if (!$BinaryDirectory) { $BinaryDirectory = Join-Path $sourceAddon 'bin' }
    $addon = Join-Path $project 'addons/crimson-framework'
    $addonBin = Join-Path $addon 'bin'
    $descriptorName = 'crimson-framework.gdextension'
    $graphExtension = 'crimsonscript'
    $binary = (Resolve-Path -LiteralPath (Join-Path $BinaryDirectory 'crimson_framework.windows.template_release.x86_64.dll')).ProviderPath
    $descriptor = Get-Content -Raw -LiteralPath (Join-Path $sourceAddon $descriptorName)
    $descriptor = $descriptor.Replace('.template_debug.', '.template_release.')
    New-Item -ItemType Directory -Path $addonBin -Force | Out-Null
    Copy-Item -LiteralPath $binary -Destination $addonBin
    $steamRuntime = Join-Path $BinaryDirectory 'steam_api64.dll'
    if (Test-Path -LiteralPath $steamRuntime) { Copy-Item -LiteralPath $steamRuntime -Destination $addonBin }
}
$descriptor | Set-Content -Encoding utf8NoBOM -LiteralPath (Join-Path $addon $descriptorName)
"res://addons/$(Split-Path -Leaf $addon)/$descriptorName" |
    Set-Content -Encoding utf8NoBOM -LiteralPath (Join-Path $project '.godot/extension_list.cfg')
@'
config_version=5

[application]
config/name="FPS Graph Performance"

[rendering]
renderer/rendering_method="gl_compatibility"
'@ | Set-Content -Encoding utf8NoBOM -LiteralPath (Join-Path $project 'project.godot')

$engineInfo = [Diagnostics.ProcessStartInfo]::new()
$engineInfo.FileName = $engine
$engineInfo.UseShellExecute = $false
$engineInfo.CreateNoWindow = $true
$engineInfo.RedirectStandardOutput = $true
$engineInfo.RedirectStandardError = $true
$engineInfo.EnvironmentVariables['APPDATA'] = $data
$engineInfo.EnvironmentVariables['LOCALAPPDATA'] = $data
foreach ($arg in @('--headless', '--path', $project, '--script', 'res://tests/visual_scripting/fps_benchmark/fps_benchmark.gd',
        '--', "--events=$Events", "--trials=$Trials")) {
    $engineInfo.ArgumentList.Add($arg)
}
if ($useOrchestrator) { $engineInfo.ArgumentList.Add('--orchestrator') }
if ($NaturalExit) { $engineInfo.ArgumentList.Add('--natural-exit') }
foreach ($scenarioId in $selectedIds) { $engineInfo.ArgumentList.Add("--scenario=$scenarioId") }

$metadata = [ordered]@{
    date_utc = [DateTime]::UtcNow.ToString('o')
    cpu = ((Get-CimInstance -ClassName Win32_Processor).Name -join ', ').Trim()
    os = [Environment]::OSVersion.VersionString
    engine = $engine
    engine_sha256 = (Get-FileHash -LiteralPath $engine).Hash
    release_dll = $binary
    release_dll_sha256 = (Get-FileHash -LiteralPath $binary).Hash
    manifest_sha256 = (Get-FileHash -LiteralPath $sourceManifest).Hash
    runner_sha256 = (Get-FileHash -LiteralPath (Join-Path $fixture 'fps_benchmark.gd')).Hash
    scenario_hashes = [ordered]@{}
}
foreach ($scenarioId in $selectedIds) {
    $metadata.scenario_hashes[$scenarioId] = @{
        graph_sha256 = (Get-FileHash -LiteralPath (Join-Path $scenarioPath "$scenarioId.$graphExtension")).Hash
        gdscript_sha256 = (Get-FileHash -LiteralPath (Join-Path $scenarioPath "$scenarioId.gd")).Hash
    }
}
Write-Host "$backend with release DLL $binary"
Write-Host "Benchmark evidence $area"

$process = [Diagnostics.Process]::Start($engineInfo)
$lines = [Collections.Generic.List[string]]::new()
$stdout = $null
$stderr = $null
$capturedStderr = $false
$exitSummary = ''
try {
    $stderr = $process.StandardError.ReadToEndAsync()
    if ($parkAfterResult) {
        $deadline = [Diagnostics.Stopwatch]::StartNew()
        while ($true) {
            $remaining = 120000 - [int]$deadline.ElapsedMilliseconds
            $nextLine = $process.StandardOutput.ReadLineAsync()
            if ($remaining -le 0 -or !$nextLine.Wait($remaining) -or $null -eq $nextLine.Result) {
                throw "Godot exited or timed out before reporting the suite result. See $logPath."
            }
            $lines.Add($nextLine.Result)
            if ($nextLine.Result.StartsWith($marker, [StringComparison]::Ordinal)) { break }
        }
        if ($process.HasExited) { throw "Orchestrator exited before the launcher could stop it. See $logPath." }
        $process.Kill($true)
        if (!$process.WaitForExit(3000)) { throw "Orchestrator did not stop after reporting. See $logPath." }
        $exitSummary = 'Launcher stopped the isolated Orchestrator process after receiving the result.'
    } else {
        $stdout = $process.StandardOutput.ReadToEndAsync()
        if (!$process.WaitForExit(120000) -or !$stdout.Wait(3000)) {
            throw "Godot did not finish within two minutes. See $logPath."
        }
        $exitSummary = "Godot exit code $($process.ExitCode)."
    }
} finally {
    if (!$process.HasExited) { $process.Kill($true) }
    $output = if ($parkAfterResult) {
        $lines -join [Environment]::NewLine
    } elseif ($stdout -and $stdout.Wait(3000)) {
        $stdout.Result
    } else {
        ''
    }
    $capturedStderr = $stderr -and $stderr.Wait(3000)
    $errors = if ($capturedStderr) { $stderr.Result } else { '' }
    $processExitCode = if ($process.HasExited) { $process.ExitCode } else { $null }
    $process.Dispose()
    Set-Content -LiteralPath $logPath -Encoding utf8NoBOM -Value @(
        ($metadata | ConvertTo-Json -Depth 8), $output, $errors, $exitSummary)
}

if (!$capturedStderr -or (!$parkAfterResult -and $processExitCode -ne 0)) {
    throw "Godot did not shut down cleanly (exit $processExitCode). See $logPath."
}
if ("$output`n$errors" -match '(?m)^\s*(SCRIPT ERROR|ERROR):' -or
    $output -notmatch ('(?m)^' + [regex]::Escape($marker) + '(.+)\r?$')) {
    throw "Godot did not report a verified benchmark. See $logPath."
}
$results = $Matches[1] | ConvertFrom-Json -AsHashtable
$warmup = [Math]::Min($Events, 128)
if ($results.backend -ne $backend -or $results.events_per_trial -ne $Events -or $results.trials -ne $Trials -or
    $results.warmup_events -ne $warmup -or ($results.scenarios.id -join ',') -cne ($selectedIds -join ',')) {
    throw "Godot reported a different workload from the launcher. See $logPath."
}

# Preserve raw samples while emitting medians and ranges in a report that can be joined by scenario ID.
foreach ($record in $results.scenarios) {
    if ($record.verified_states -ne 3 * ($Events * $Trials + $warmup) -or $record.cases -le 0) {
        throw "Missing verified states for $($record.id). See $logPath."
    }
    foreach ($lane in @('gdscript', 'graph', 'signal_only')) {
        $samples = @($record[$lane + '_usec'] | Sort-Object)
        if ($samples.Count -ne $Trials -or $samples[0] -lt 0 -or ($lane -ne 'signal_only' -and $samples[-1] -eq 0)) {
            throw "Invalid $lane samples for $($record.id). See $logPath."
        }
        $middle = [int][Math]::Floor($Trials / 2)
        $median = if ($Trials % 2) { $samples[$middle] } else { ($samples[$middle - 1] + $samples[$middle]) / 2.0 }
        $record[$lane + '_median_usec'] = $median
        $record[$lane + '_min_usec'] = $samples[0]
        $record[$lane + '_max_usec'] = $samples[-1]
    }
    $summary = [string]::Format([Globalization.CultureInfo]::InvariantCulture,
        '{0,-27} GDScript {1,8:F3} ms   {2,-12} {3,8:F3} ms   signal {4,6:F3} ms',
        $record.id, ($record.gdscript_median_usec / 1000.0), $backend,
        ($record.graph_median_usec / 1000.0), ($record.signal_only_median_usec / 1000.0))
    Write-Host $summary
}
$results.metadata = $metadata
$results.shutdown = $exitSummary
$resultsPath = Join-Path $area 'results.json'
Set-Content -LiteralPath $resultsPath -Encoding utf8NoBOM -Value ($results | ConvertTo-Json -Depth 10)
Write-Output $resultsPath
