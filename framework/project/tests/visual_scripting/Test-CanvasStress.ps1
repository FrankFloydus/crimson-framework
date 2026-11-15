#Requires -Version 7.0
# Copyright Francesco Di Blasi 2026 - All Rights Reserved
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$GodotExe,
    [string]$BinaryDirectory,
    [ValidateSet('forward_plus', 'gl_compatibility')][string]$Renderer = 'forward_plus'
)

$ErrorActionPreference = 'Stop'
$engine = (Resolve-Path -LiteralPath $GodotExe).ProviderPath
$sourceAddon = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../../addons/crimson-framework')).ProviderPath
if (!$BinaryDirectory) { $BinaryDirectory = Join-Path $sourceAddon 'bin' }
$dll = (Resolve-Path -LiteralPath (Join-Path $BinaryDirectory 'crimson_framework.windows.template_debug.x86_64.dll')).ProviderPath
$area = Join-Path ([IO.Path]::GetTempPath()) ('crimson-canvas-stress-' + [guid]::NewGuid().ToString('N'))
$project = Join-Path $area 'project'
$addon = Join-Path $project 'addons/crimson-framework'
$plugin = Join-Path $project 'addons/canvas_stress'
$data = Join-Path $area 'data'
$log = Join-Path $area 'benchmark.log'
New-Item -ItemType Directory -Path (Join-Path $addon 'bin'), (Join-Path $project '.godot'), (Join-Path $data 'Godot'), $plugin -Force | Out-Null
Copy-Item -LiteralPath $dll -Destination (Join-Path $addon 'bin')
foreach ($filename in @('editor', 'images', 'crimson-framework.gdextension')) {
    $source = Join-Path $sourceAddon $filename
    if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination $addon -Recurse }
}
$steamRuntime = Join-Path $BinaryDirectory 'steam_api64.dll'
if (Test-Path -LiteralPath $steamRuntime) { Copy-Item -LiteralPath $steamRuntime -Destination (Join-Path $addon 'bin') }
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'canvas_stress.gd') -Destination $plugin
'res://addons/crimson-framework/crimson-framework.gdextension' |
    Set-Content -Encoding utf8NoBOM -LiteralPath (Join-Path $project '.godot/extension_list.cfg')
@'
; Copyright Francesco Di Blasi 2026 - All Rights Reserved

[plugin]

name="Visual Script Canvas Stress Fixture"
script="canvas_stress.gd"
'@ | Set-Content -Encoding utf8NoBOM -LiteralPath (Join-Path $plugin 'plugin.cfg')
@'
[gd_resource type="EditorSettings" format=3]

[resource]
interface/editor/appearance/display_scale = 3
'@ | Set-Content -Encoding utf8NoBOM -LiteralPath (Join-Path $data 'Godot/editor_settings-4.7.tres')
@'
config_version=5

[application]
config/name="Crimson Visual Script Canvas Stress"

[editor_plugins]
enabled=PackedStringArray("res://addons/canvas_stress/plugin.cfg")
'@ | Set-Content -Encoding utf8NoBOM -LiteralPath (Join-Path $project 'project.godot')

# Run a real editor with its own settings, without touching the developer's editor or desktop pointer.
$start = [Diagnostics.ProcessStartInfo]::new()
$start.FileName = $engine
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$start.EnvironmentVariables['APPDATA'] = $data
$start.EnvironmentVariables['LOCALAPPDATA'] = $data
foreach ($argument in @('--path', $project, '--editor', '--rendering-method', $Renderer,
        '--resolution', '1440x900', '--disable-vsync', '--max-fps', '0', '--dap-port', '37768', '--lsp-port', '37767')) {
    $start.ArgumentList.Add($argument)
}

$dllHash = (Get-FileHash -LiteralPath $dll).Hash
Write-Host "Debug DLL SHA-256 $dllHash"
Write-Host "Isolated log $log"
$process = [Diagnostics.Process]::Start($start)
try {
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    $finished = $process.WaitForExit(120000)
    if (!$finished) {
        $process.Kill($true)
        $process.WaitForExit()
    }
    if (!$stdout.Wait(3000) -or !$stderr.Wait(3000)) {
        throw "Could not read the isolated Godot process output. See $area."
    }
    $output = $stdout.Result + [Environment]::NewLine + $stderr.Result
    [IO.File]::WriteAllText($log, $output)
    if (!$finished -or $process.ExitCode -ne 0 -or $output -match '(?m)^\s*(SCRIPT ERROR|ERROR):') {
        throw "Godot did not finish the canvas stress test. See $log."
    }
    if ($output -notmatch '(?m)^CRIMSON_CANVAS_STRESS_PASSED (.+)\r?$') {
        throw "The canvas stress test did not report results. See $log."
    }
    $results = $Matches[1] | ConvertFrom-Json
} finally {
    if (!$process.HasExited) { $process.Kill($true) }
    $process.Dispose()
}

$results | Add-Member -NotePropertyName 'dll_sha256' -NotePropertyValue $dllHash
$results | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8NoBOM -LiteralPath (Join-Path $area 'results.json')
Write-Host "Godot $($results.godot), $($results.renderer), $($results.viewport_size), editor scale $($results.editor_scale)"
$results.results | Select-Object nodes, wires, motions_per_frame,
    @{ Label = 'Frame p50 ms'; Expression = { $_.frame_p50_usec / 1000 } },
    @{ Label = 'Frame p95 ms'; Expression = { $_.frame_p95_usec / 1000 } },
    @{ Label = 'Input p95 ms'; Expression = { $_.input_p95_usec / 1000 } } | Format-Table
