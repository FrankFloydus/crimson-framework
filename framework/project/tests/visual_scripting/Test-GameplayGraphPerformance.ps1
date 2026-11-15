#Requires -Version 7.0
# Copyright Francesco Di Blasi 2026 - All Rights Reserved
[CmdletBinding(DefaultParameterSetName = 'Crimson')]
param(
    [Parameter(Mandatory)][string]$GodotExe,
    [Parameter(ParameterSetName = 'Crimson')][string]$BinaryDirectory,
    [Parameter(Mandatory, ParameterSetName = 'Orchestrator')][string]$OrchestratorAddon,
    [Parameter(Mandatory, ParameterSetName = 'Orchestrator')][string]$OrchestratorGraph,
    [Parameter(ParameterSetName = 'Orchestrator')][switch]$NaturalExit
)

$ErrorActionPreference = 'Stop'
if (!$IsWindows) { throw 'This harness uses the Windows x86-64 release addons.' }
$engine = (Resolve-Path -LiteralPath $GodotExe).ProviderPath
$useOrchestrator = $PSCmdlet.ParameterSetName -eq 'Orchestrator'
$parkAfterResult = $useOrchestrator -and !$NaturalExit
$marker = 'GAMEPLAY_GRAPH_BENCHMARK_PASSED '
$area = Join-Path ([IO.Path]::GetTempPath()) ('crimson-gameplay-graph-performance-' + [guid]::NewGuid().ToString('N'))
$project = Join-Path $area 'project'
$data = Join-Path $area 'data'
$fixture = Join-Path $project 'tests/visual_scripting'
$logPath = Join-Path $area 'benchmark.log'
New-Item -ItemType Directory -Path (Join-Path $project '.godot'), $data, $fixture -Force | Out-Null
foreach ($file in @('gameplay_benchmark.gd', 'gameplay_pickup.gd')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination $fixture
}

# Register one backend in a private project and force its release binary for either Godot selector.
if ($useOrchestrator) {
    $backend = 'orchestrator'
    $backendLabel = 'Orchestrator'
    $sourceAddon = (Resolve-Path -LiteralPath $OrchestratorAddon).ProviderPath
    $descriptorName = 'orchestrator.gdextension'
    $releaseName = 'orchestrator.windows.64.release.dll'
    $binary = (Resolve-Path -LiteralPath (Join-Path $sourceAddon $releaseName)).ProviderPath
    $sourceGraph = (Resolve-Path -LiteralPath $OrchestratorGraph).ProviderPath
    if ([IO.Path]::GetExtension($sourceGraph) -ne '.torch') {
        throw 'Pass an Orchestrator .torch graph for -OrchestratorGraph.'
    }
    $addon = Join-Path $project 'addons/orchestrator'
    $descriptor = Get-Content -Raw -LiteralPath (Join-Path $sourceAddon $descriptorName)
    if ($descriptor -notmatch '(?m)^\s*windows\.x86_64\s*=\s*"res://addons/orchestrator/orchestrator\.windows\.64\.release\.dll"\s*$' -or
        $descriptor -notmatch '(?m)^\s*windows\.debug\.x86_64\s*=\s*"res://addons/orchestrator/orchestrator\.windows\.64\.editor\.dll"\s*$') {
        throw 'Pass the official Orchestrator Windows x86-64 addon directory for -OrchestratorAddon.'
    }
    $descriptor = $descriptor.Replace('orchestrator.windows.64.editor.dll', $releaseName)
    New-Item -ItemType Directory -Path $addon -Force | Out-Null
    Copy-Item -LiteralPath $binary -Destination $addon
    Copy-Item -LiteralPath $sourceGraph -Destination (Join-Path $fixture 'gameplay_pickup.torch')
} else {
    $backend = 'crimson'
    $backendLabel = 'Crimson Visual Script'
    $sourceAddon = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../../addons/crimson-framework')).ProviderPath
    if (!$BinaryDirectory) { $BinaryDirectory = Join-Path $sourceAddon 'bin' }
    $descriptorName = 'crimson-framework.gdextension'
    $releaseName = 'crimson_framework.windows.template_release.x86_64.dll'
    $binary = (Resolve-Path -LiteralPath (Join-Path $BinaryDirectory $releaseName)).ProviderPath
    $sourceGraph = Join-Path $PSScriptRoot 'gameplay_pickup.crimsonscript'
    $addon = Join-Path $project 'addons/crimson-framework'
    $addonBin = Join-Path $addon 'bin'
    $descriptor = Get-Content -Raw -LiteralPath (Join-Path $sourceAddon $descriptorName)
    $descriptor = $descriptor.Replace('.template_debug.', '.template_release.')
    New-Item -ItemType Directory -Path $addonBin -Force | Out-Null
    Copy-Item -LiteralPath $binary -Destination $addonBin
    $steamRuntime = Join-Path $BinaryDirectory 'steam_api64.dll'
    if (Test-Path -LiteralPath $steamRuntime) { Copy-Item -LiteralPath $steamRuntime -Destination $addonBin }
    Copy-Item -LiteralPath $sourceGraph -Destination $fixture
}
$descriptor | Set-Content -Encoding utf8NoBOM -LiteralPath (Join-Path $addon $descriptorName)
"res://addons/$(Split-Path -Leaf $addon)/$descriptorName" |
    Set-Content -Encoding utf8NoBOM -LiteralPath (Join-Path $project '.godot/extension_list.cfg')
@'
config_version=5

[application]
config/name="Gameplay Graph Performance"

[rendering]
renderer/rendering_method="gl_compatibility"
'@ | Set-Content -Encoding utf8NoBOM -LiteralPath (Join-Path $project 'project.godot')

$start = [Diagnostics.ProcessStartInfo]::new()
$start.FileName = $engine
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$start.EnvironmentVariables['APPDATA'] = $data
$start.EnvironmentVariables['LOCALAPPDATA'] = $data
foreach ($argument in @('--headless', '--path', $project, '--script', 'res://tests/visual_scripting/gameplay_benchmark.gd')) {
    $start.ArgumentList.Add($argument)
}
if ($useOrchestrator) {
    $start.ArgumentList.Add('--')
    $start.ArgumentList.Add('--orchestrator')
    if ($NaturalExit) { $start.ArgumentList.Add('--natural-exit') }
}

$runHeader = @(
    "CPU $(((Get-CimInstance -ClassName Win32_Processor).Name -join ', ').Trim())"
    "OS $([Environment]::OSVersion.VersionString)"
    "Backend $backendLabel"
    "Godot $engine"
    "Release DLL $binary"
    "Release DLL SHA-256 $((Get-FileHash -LiteralPath $binary).Hash)"
    "Graph $sourceGraph"
    "Graph SHA-256 $((Get-FileHash -LiteralPath $sourceGraph).Hash)"
    "Isolated log $logPath"
)
Write-Host ($runHeader -join [Environment]::NewLine)

$process = [Diagnostics.Process]::Start($start)
$lines = [Collections.Generic.List[string]]::new()
$output = ''
$exitSummary = ''
$stdout = $null
$stderr = $null
$capturedStderr = $false
try {
    $stderr = $process.StandardError.ReadToEndAsync()
    if ($parkAfterResult) {
        # The isolated Orchestrator runner parks after reporting every trial to avoid its DLL unload fault.
        $deadline = [Diagnostics.Stopwatch]::StartNew()
        while ($true) {
            $remaining = 120000 - [int]$deadline.ElapsedMilliseconds
            $nextLine = $process.StandardOutput.ReadLineAsync()
            if ($remaining -le 0 -or !$nextLine.Wait($remaining) -or $null -eq $nextLine.Result) {
                throw "Godot exited or timed out before the benchmark result. See $logPath."
            }
            $lines.Add($nextLine.Result)
            if ($nextLine.Result.StartsWith($marker, [StringComparison]::Ordinal)) { break }
        }
        if ($process.HasExited) { throw "Orchestrator exited before the launcher could stop it. See $logPath." }
        $process.Kill($true)
        if (!$process.WaitForExit(3000)) { throw "Godot did not stop after reporting the result. See $logPath." }
        $exitSummary = 'The launcher stopped the isolated Godot process after receiving the result marker.'
    } else {
        $stdout = $process.StandardOutput.ReadToEndAsync()
        if (!$process.WaitForExit(120000) -or !$stdout.Wait(3000)) {
            throw "Godot did not finish the benchmark within two minutes. See $logPath."
        }
        $exitSummary = "Godot exit code $($process.ExitCode)."
    }
} finally {
    if (!$process.HasExited) { $process.Kill($true) }
    if ($parkAfterResult) {
        $output = $lines -join [Environment]::NewLine
    } elseif ($stdout -and $stdout.Wait(3000)) {
        $output = $stdout.Result
    }
    $capturedStderr = $stderr -and $stderr.Wait(3000)
    $errors = if ($capturedStderr) { $stderr.Result } else { '' }
    $processExitCode = if ($process.HasExited) { $process.ExitCode } else { $null }
    $process.Dispose()
    Set-Content -LiteralPath $logPath -Encoding utf8NoBOM -Value ($runHeader + $output + $errors + $exitSummary)
}

Write-Host $output.TrimEnd()
if ($errors) { Write-Host $errors.TrimEnd() }
Write-Host $exitSummary
if (!$capturedStderr) {
    throw "Godot did not finish writing its error stream. See $logPath."
}
if (!$parkAfterResult -and $processExitCode -ne 0) {
    throw "Godot did not shut down cleanly (exit $processExitCode). See $logPath."
}
if ("$output`n$errors" -match '(?m)^\s*(SCRIPT ERROR|ERROR):' -or
    $output -notmatch ('(?m)^' + [regex]::Escape($marker) + '(.+)\r?$')) {
    throw "Godot did not report a successful benchmark. See $logPath."
}
$results = $Matches[1] | ConvertFrom-Json
if ($results.backend -ne $backend -or $results.events_per_trial -ne 8192 -or $results.warmup_events -ne 256 -or
    $results.checksum -ne 673792) {
    throw "Godot reported an unexpected backend, event count or checksum. See $logPath."
}

# Report raw timings without subtracting the signal emission control.
foreach ($run in @(
    @{ Label = 'Signal emission without a listener'; Samples = $results.signal_only_usec }
    @{ Label = 'GDScript'; Samples = $results.gdscript_usec }
    @{ Label = $backendLabel; Samples = $results.graph_usec }
)) {
    $samples = @($run.Samples | Sort-Object)
    if ($samples.Count -ne 5 -or $samples[0] -le 0) {
        throw "Expected five positive measurements for $($run.Label). See $logPath."
    }
    $summary = [string]::Format([Globalization.CultureInfo]::InvariantCulture,
        '{0} median {1:F3} ms (range {2:F3}-{3:F3} ms)', $run.Label, ($samples[2] / 1000.0),
        ($samples[0] / 1000.0), ($samples[-1] / 1000.0))
    Write-Host $summary
}
