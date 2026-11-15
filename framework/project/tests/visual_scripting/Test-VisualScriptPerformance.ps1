#Requires -Version 7.0
# Copyright Francesco Di Blasi 2026 - All Rights Reserved
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$GodotExe,
    [string]$BinaryDirectory
)

$ErrorActionPreference = 'Stop'
$engine = (Resolve-Path -LiteralPath $GodotExe).ProviderPath
$sourceAddon = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../../addons/crimson-framework')).ProviderPath
if (!$BinaryDirectory) { $BinaryDirectory = Join-Path $sourceAddon 'bin' }
$releaseDll = 'crimson_framework.windows.template_release.x86_64.dll'
$binary = (Resolve-Path -LiteralPath (Join-Path $BinaryDirectory $releaseDll)).ProviderPath
$descriptorName = 'crimson-framework.gdextension'
$area = Join-Path ([IO.Path]::GetTempPath()) ('crimson-visual-script-performance-' + [guid]::NewGuid().ToString('N'))
$project = Join-Path $area 'project'
$addon = Join-Path $project 'addons/crimson-framework'
$data = Join-Path $area 'data'
$fixture = Join-Path $project 'tests/visual_scripting'
$logPath = Join-Path $area 'benchmark.log'
New-Item -ItemType Directory -Path (Join-Path $addon 'bin'), (Join-Path $project '.godot'), $data, $fixture -Force | Out-Null
Copy-Item -LiteralPath $binary -Destination (Join-Path $addon 'bin')
$steamRuntime = Join-Path $BinaryDirectory 'steam_api64.dll'
if (Test-Path -LiteralPath $steamRuntime) {
    Copy-Item -LiteralPath $steamRuntime -Destination (Join-Path $addon 'bin')
}
foreach ($file in @('loop_benchmark.gd', 'loop_benchmark.crimsonscript')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination $fixture
}

# The Godot editor executable identifies as debug. Point both selectors to the release DLL in this isolated project.
$descriptor = Get-Content -Raw -LiteralPath (Join-Path $sourceAddon $descriptorName)
$descriptor.Replace('.template_debug.', '.template_release.') |
    Set-Content -Encoding utf8NoBOM -LiteralPath (Join-Path $addon $descriptorName)
"res://addons/crimson-framework/$descriptorName" |
    Set-Content -Encoding utf8NoBOM -LiteralPath (Join-Path $project '.godot/extension_list.cfg')
@'
config_version=5

[application]
config/name="Crimson Visual Script Performance"

[rendering]
renderer/rendering_method="gl_compatibility"
'@ | Set-Content -Encoding utf8NoBOM -LiteralPath (Join-Path $project 'project.godot')

# Redirect both streams and isolate Godot user data from the actual project.
$start = [Diagnostics.ProcessStartInfo]::new()
$start.FileName = $engine
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$start.EnvironmentVariables['APPDATA'] = $data
$start.EnvironmentVariables['LOCALAPPDATA'] = $data
foreach ($argument in @('--headless', '--path', $project, '--script', 'res://tests/visual_scripting/loop_benchmark.gd')) {
    $start.ArgumentList.Add($argument)
}

Write-Host "CPU $(((Get-CimInstance -ClassName Win32_Processor).Name -join ', ').Trim())"
Write-Host "OS $([Environment]::OSVersion.VersionString)"
Write-Host "Release DLL $binary"
Write-Host "Isolated log $logPath"
$process = [Diagnostics.Process]::Start($start)
try {
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    if (!$process.WaitForExit(600000) -or !$stdout.Wait(3000) -or !$stderr.Wait(3000)) {
        throw "Godot did not finish the benchmark within ten minutes. See $area."
    }
    $output = $stdout.Result + [Environment]::NewLine + $stderr.Result
    [IO.File]::WriteAllText($logPath, $output)
    Write-Host $output.TrimEnd()
    if ($process.ExitCode -ne 0 -or $output -match '(?m)^\s*(SCRIPT ERROR|ERROR):') {
        throw "Godot failed the benchmark (exit $($process.ExitCode)). See $logPath."
    }
    if ($output -notmatch '(?m)^CRIMSON_VISUAL_SCRIPT_BENCHMARK_PASSED (.+)\r?$') {
        throw "The benchmark did not report complete results. See $logPath."
    }
    $results = $Matches[1] | ConvertFrom-Json
} finally {
    if (!$process.HasExited) { $process.Kill($true) }
    $process.Dispose()
}

# Report wall-clock medians and observed ranges without enforcing a machine-specific threshold.
$gdscriptSamples = @($results.gdscript_usec | Sort-Object)
$visualSamples = @($results.visual_script_usec | Sort-Object)
$medianIndex = [int][Math]::Floor($gdscriptSamples.Count / 2)
$gdscriptMedian = $gdscriptSamples[$medianIndex] / 1000.0
$visualMedian = $visualSamples[$medianIndex] / 1000.0
Write-Host ('GDScript median {0:F3} ms (range {1:F3}-{2:F3} ms)' -f $gdscriptMedian, ($gdscriptSamples[0] / 1000.0), ($gdscriptSamples[-1] / 1000.0))
Write-Host ('Visual Script median {0:F3} ms (range {1:F3}-{2:F3} ms)' -f $visualMedian, ($visualSamples[0] / 1000.0), ($visualSamples[-1] / 1000.0))
Write-Host ('Visual Script took {0:F2}x as long ({1:F2}% lower throughput)' -f ($visualMedian / $gdscriptMedian), (100.0 * (1.0 - $gdscriptMedian / $visualMedian)))
