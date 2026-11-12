#Requires -Version 7.0
# Copyright Francesco Di Blasi 2026 - All Rights Reserved
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$GodotExe,
    [Parameter(Mandatory)][string]$WindowsDebugTemplate,
    [string]$WorkDirectory = [IO.Path]::GetTempPath()
)

$ErrorActionPreference = 'Stop'
$engine = (Resolve-Path -LiteralPath $GodotExe).ProviderPath
$template = (Resolve-Path -LiteralPath $WindowsDebugTemplate).ProviderPath.Replace('\', '/')
$work = (Resolve-Path -LiteralPath $WorkDirectory).ProviderPath
$framework = Split-Path -Parent $PSScriptRoot
$sourceAddon = Join-Path $framework 'project/addons/crimson-framework'
$crimson = 'crimson_framework.windows.template_debug.x86_64.dll'
$steam = 'steam_api64.dll'

# Run Godot against the staged project and keep its export diagnostics.
function Invoke-Editor {
    param([string]$Log, [string[]]$Arguments)

    & $engine --headless --path $project --frame-delay 1000 @Arguments *> $Log
    if ($LASTEXITCODE -ne 0 -or (Select-String -LiteralPath $Log -Pattern '^\s*(SCRIPT ERROR|ERROR):' -Quiet)) {
        throw "Godot editor command failed (exit $LASTEXITCODE). See $Log."
    }
}

# Boot one packaged executable with isolated user data and a bounded timeout.
function Test-ExportedGame {
    param([string]$Executable, [string]$Data)

    New-Item -ItemType Directory -Path $Data | Out-Null
    $start = [Diagnostics.ProcessStartInfo]::new($Executable)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.ArgumentList.Add('--headless')
    $start.EnvironmentVariables['APPDATA'] = $Data
    $start.EnvironmentVariables['LOCALAPPDATA'] = $Data
    $process = [Diagnostics.Process]::Start($start)
    try {
        if (!$process.WaitForExit(20000)) {
            $process.Kill($true)
            throw "Exported game timed out: $Executable"
        }
        if ($process.ExitCode -ne 0) { throw "Exported game returned $($process.ExitCode): $Executable" }
    } finally {
        $process.Dispose()
    }
}

$area = Join-Path $work ('crimson-windows-exports-' + [guid]::NewGuid().ToString('N'))
$project = Join-Path $area 'project'
$addon = Join-Path $project 'addons/crimson-framework'
New-Item -ItemType Directory -Path (Join-Path $addon 'bin') -Force | Out-Null
Write-Host "Windows export evidence: $area"
Copy-Item -LiteralPath (Join-Path $sourceAddon 'crimson-framework.gdextension') -Destination $addon
Copy-Item -LiteralPath (Join-Path $sourceAddon 'images') -Destination $addon -Recurse
foreach ($file in @($crimson, $steam)) {
    $source = Join-Path $sourceAddon "bin/$file"
    if (!(Test-Path -LiteralPath $source -PathType Leaf)) { throw "Build the debug DLL with Steamworks first. Missing $source." }
    Copy-Item -LiteralPath $source -Destination (Join-Path $addon 'bin')
}

@'
config_version=5
[application]
config/name="Crimson Windows Export Fixture"
run/main_scene="res://main.tscn"
[rendering]
renderer/rendering_method="gl_compatibility"
'@ | Set-Content -Encoding utf8NoBOM -LiteralPath (Join-Path $project 'project.godot')

@'
[gd_scene format=3]
[ext_resource type="Script" path="res://boot.gd" id="1"]
[node name="ExportFixture" type="Node"]
script = ExtResource("1")
'@ | Set-Content -Encoding utf8NoBOM -LiteralPath (Join-Path $project 'main.tscn')

@'
extends Node

# Verify the framework's native classes are available in the exported game.
func _ready() -> void:
	get_tree().quit(0 if ClassDB.class_exists("GameRuntime") else 1)
'@ | Set-Content -Encoding utf8NoBOM -LiteralPath (Join-Path $project 'boot.gd')

$presets = @(
    @{Name='direct'; Tags=''; Files=@()},
    @{Name='steam'; Tags='crimson_steam'; Files=@($steam)}
)
$config = for ($index = 0; $index -lt $presets.Count; $index++) {
    $preset = $presets[$index]
    @"
[preset.$index]
name="$($preset.Name)"
platform="Windows Desktop"
runnable=false
dedicated_server=false
custom_features="$($preset.Tags)"
export_filter="all_resources"
include_filter=""
exclude_filter=""
export_path=""

[preset.$index.options]
custom_template/debug="$template"
debug/export_console_wrapper=0
binary_format/architecture="x86_64"
binary_format/embed_pck=true
application/modify_resources=false

"@
}
$config | Set-Content -Encoding utf8NoBOM -LiteralPath (Join-Path $project 'export_presets.cfg')
Invoke-Editor -Log (Join-Path $area 'import.log') -Arguments @('--editor', '--import')

foreach ($preset in $presets) {
    $output = (New-Item -ItemType Directory -Path (Join-Path $area $preset.Name)).FullName
    $exe = Join-Path $output 'CrimsonExport.exe'
    Invoke-Editor -Log (Join-Path $area "$($preset.Name)-export.log") -Arguments @('--export-debug', $preset.Name, $exe)
    foreach ($file in @($crimson, $steam)) {
        $path = Join-Path $output $file
        if ($file -eq $crimson -or $file -in $preset.Files) {
            $original = Join-Path $addon "bin/$file"
            if (!(Test-Path -LiteralPath $path -PathType Leaf) -or
                (Get-FileHash -LiteralPath $path).Hash -ne (Get-FileHash -LiteralPath $original).Hash) {
                throw "Preset $($preset.Name) did not export the expected $file beside its executable."
            }
        } elseif (Test-Path -LiteralPath $path) {
            throw "Preset $($preset.Name) unexpectedly exported $file."
        }
    }
    Test-ExportedGame -Executable $exe -Data (Join-Path $area "$($preset.Name)-data")
    Write-Host "CRIMSON_WINDOWS_EXPORT_PASSED $($preset.Name)"
}
