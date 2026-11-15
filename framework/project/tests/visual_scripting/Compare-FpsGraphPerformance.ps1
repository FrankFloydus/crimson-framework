#Requires -Version 7.0
# Copyright Francesco Di Blasi 2026 - All Rights Reserved
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Crimson,
    [Parameter(Mandatory)][string]$Orchestrator,
    [string]$Before,
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'

# Read verified evidence emitted by Test-FpsGraphPerformance.ps1.
function Read-Results([string]$Path, [string]$Backend) {
    $result = Get-Content -Raw -LiteralPath $Path | ConvertFrom-Json
    if ($result.backend -ne $Backend -or !$result.scenarios.Count) {
        throw "Expected verified $Backend results at $Path."
    }
    foreach ($case in $result.scenarios) {
        if ($case.graph_median_usec -le 0 -or $case.gdscript_median_usec -le 0) {
            throw "A timer sample for $($case.id) is too short for comparison. Increase -Events."
        }
    }
    return $result
}

# Keep comparisons limited to the same workload, runtime, and hardware.
function Assert-Comparable($Reference, $Candidate) {
    foreach ($key in @('events_per_trial', 'warmup_events', 'trials')) {
        if ($Reference.$key -ne $Candidate.$key) { throw "Runs disagree on $key." }
    }
    foreach ($key in @('engine_sha256', 'manifest_sha256', 'runner_sha256', 'cpu', 'os')) {
        if (!$Reference.metadata.$key -or $Reference.metadata.$key -ne $Candidate.metadata.$key) {
            throw "Runs disagree on $key."
        }
    }
    if (($Reference.scenarios.id -join ',') -cne ($Candidate.scenarios.id -join ',')) {
        throw 'Runs contain different scenarios.'
    }
    foreach ($scenarioId in $Reference.scenarios.id) {
        if ($Reference.metadata.scenario_hashes.$scenarioId.gdscript_sha256 -ne
            $Candidate.metadata.scenario_hashes.$scenarioId.gdscript_sha256) {
            throw "Runs use different GDScript fixtures for $scenarioId."
        }
    }
}

# Compute a multiplicative average so larger workloads do not dominate the ratio.
function Get-GeometricMean([double[]]$Ratios) {
    $logSum = 0.0
    foreach ($ratio in $Ratios) { $logSum += [Math]::Log($ratio) }
    return [Math]::Exp($logSum / $Ratios.Count)
}

# Format timings with a decimal point regardless of PowerShell's locale.
function Format-Number([double]$Number, [string]$Format = 'F3') {
    return $Number.ToString($Format, [Globalization.CultureInfo]::InvariantCulture)
}

$crimsonRun = Read-Results $Crimson 'crimson'
$orchestratorRun = Read-Results $Orchestrator 'orchestrator'
$beforeRun = if ($Before) { Read-Results $Before 'crimson' } else { $null }
Assert-Comparable $crimsonRun $orchestratorRun
if ($beforeRun) {
    Assert-Comparable $crimsonRun $beforeRun
    foreach ($scenarioId in $crimsonRun.scenarios.id) {
        if ($crimsonRun.metadata.scenario_hashes.$scenarioId.graph_sha256 -ne
            $beforeRun.metadata.scenario_hashes.$scenarioId.graph_sha256) {
            throw "Crimson runs use different graphs for $scenarioId."
        }
    }
}

$rows = for ($index = 0; $index -lt $crimsonRun.scenarios.Count; $index++) {
    $candidate = $crimsonRun.scenarios[$index]
    $peer = $orchestratorRun.scenarios[$index]
    $row = [ordered]@{
        scenario = $candidate.id
        family = $candidate.family
        label = $candidate.label
        crimson_us = $candidate.graph_median_usec
        crimson_min_us = $candidate.graph_min_usec
        crimson_max_us = $candidate.graph_max_usec
        gdscript_crimson_us = $candidate.gdscript_median_usec
        orchestrator_us = $peer.graph_median_usec
        orchestrator_min_us = $peer.graph_min_usec
        orchestrator_max_us = $peer.graph_max_usec
        gdscript_orchestrator_us = $peer.gdscript_median_usec
        signal_crimson_us = $candidate.signal_only_median_usec
        signal_orchestrator_us = $peer.signal_only_median_usec
        crimson_over_gdscript = $candidate.graph_median_usec / $candidate.gdscript_median_usec
        orchestrator_over_gdscript = $peer.graph_median_usec / $peer.gdscript_median_usec
        crimson_over_orchestrator = $candidate.graph_median_usec / $peer.graph_median_usec
        crimson_verified_states = $candidate.verified_states
        orchestrator_verified_states = $peer.verified_states
    }
    if ($beforeRun) {
        $previous = $beforeRun.scenarios[$index]
        $row['crimson_before_us'] = $previous.graph_median_usec
        $row['crimson_before_min_us'] = $previous.graph_min_usec
        $row['crimson_before_max_us'] = $previous.graph_max_usec
        $row['crimson_before_over_after'] = $previous.graph_median_usec / $candidate.graph_median_usec
    }
    [pscustomobject]$row
}

if (!$OutputDirectory) {
    $OutputDirectory = [IO.Path]::GetDirectoryName((Resolve-Path -LiteralPath $Crimson).ProviderPath)
}
$target = (New-Item -ItemType Directory -Path $OutputDirectory -Force).FullName
$rows | Export-Csv -Encoding utf8NoBOM -NoTypeInformation -LiteralPath (Join-Path $target 'comparison.csv')
$lines = [Collections.Generic.List[string]]::new()
$lines.Add('# FPS gameplay graph comparison')
$lines.Add('')
$lines.Add("Godot ran $($crimsonRun.trials) trials of $($crimsonRun.events_per_trial) events per scenario and verified every body state. Times are raw medians with the signal emission cost included.")
$lines.Add('')
$lines.Add('| Family | Scenarios | Crimson below Orchestrator | Crimson / GDScript | Orchestrator / GDScript | Crimson / Orchestrator |')
$lines.Add('| --- | ---: | ---: | ---: | ---: | ---: |')
$families = @($rows | Group-Object family)
$families += [pscustomobject]@{ Name = 'All scenarios'; Count = $rows.Count; Group = $rows }
foreach ($family in $families) {
    $wins = @($family.Group | Where-Object { $_.crimson_over_orchestrator -lt 1 }).Count
    $crimsonRatio = Format-Number (Get-GeometricMean $family.Group.crimson_over_gdscript) 'F2'
    $orchestratorRatio = Format-Number (Get-GeometricMean $family.Group.orchestrator_over_gdscript) 'F2'
    $peerRatio = Format-Number (Get-GeometricMean $family.Group.crimson_over_orchestrator) 'F2'
    $lines.Add("| $($family.Name) | $($family.Count) | $wins | ${crimsonRatio}x | ${orchestratorRatio}x | ${peerRatio}x |")
}
$lines.Add('')
$beforeColumn = if ($beforeRun) { ' Before (ms) |' } else { '' }
$beforeDivider = if ($beforeRun) { ' ---: |' } else { '' }
$lines.Add("| Scenario | GDScript with Crimson (ms) | Crimson (ms) | GDScript with Orchestrator (ms) | Orchestrator (ms) |$beforeColumn")
$lines.Add("| --- | ---: | ---: | ---: | ---: |$beforeDivider")
foreach ($row in $rows) {
    $gd = Format-Number ($row.gdscript_crimson_us / 1000)
    $cr = Format-Number ($row.crimson_us / 1000)
    $orcGd = Format-Number ($row.gdscript_orchestrator_us / 1000)
    $orc = Format-Number ($row.orchestrator_us / 1000)
    $baseline = if ($beforeRun) { ' ' + (Format-Number ($row.crimson_before_us / 1000)) + ' |' } else { '' }
    $lines.Add("| $($row.label) | $gd | $cr | $orcGd | $orc |$baseline")
}
if ($beforeRun) {
    $overallGain = Format-Number (Get-GeometricMean $rows.crimson_before_over_after) 'F2'
    $lines.Add('')
    $lines.Add("The geometric mean before / after Crimson time across these scenarios was ${overallGain}x.")
}
$reportPath = Join-Path $target 'comparison.md'
Set-Content -Encoding utf8NoBOM -LiteralPath $reportPath -Value $lines
Write-Output $reportPath
