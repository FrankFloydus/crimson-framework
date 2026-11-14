#Requires -Version 7.0
# Copyright Francesco Di Blasi 2026 - All Rights Reserved
<#
.SYNOPSIS
Measures the native wire codec and actual Godot RPC serialization in isolated Release fixtures.
.DESCRIPTION
Builds a standalone native benchmark with MSVC optimization. Uses the supplied Godot
console to benchmark the Release addon with real SceneMultiplayer encode/decode and an
in-process packet peer. Records JSON results and raw logs in a fresh work directory.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$GodotExe,
    [int[]]$Recipients = @(1, 8, 32, 128, 256),
    [string]$BinaryDirectory,
    [string]$WorkDirectory = [IO.Path]::GetTempPath()
)

$ErrorActionPreference = 'Stop'
$framework = Split-Path -Parent $PSScriptRoot
$engine = (Resolve-Path -LiteralPath $GodotExe).ProviderPath
$work = (Resolve-Path -LiteralPath $WorkDirectory).ProviderPath
$binarySource = if ($BinaryDirectory) {
    (Resolve-Path -LiteralPath $BinaryDirectory).ProviderPath
} else {
    Join-Path $framework 'project/addons/crimson-framework/bin'
}
$binaryName = 'crimson_framework.windows.template_release.x86_64.dll'
$binary = Join-Path $binarySource $binaryName
if (!(Test-Path -LiteralPath $binary -PathType Leaf)) {
    throw "Build the Release addon before running this benchmark. Missing $binary."
}
if (!$Recipients -or ($Recipients | Where-Object { $_ -lt 1 -or $_ -gt 512 })) {
    throw 'Specify recipient counts from 1 to 512.'
}

$area = Join-Path $work ("crimson-serialization-" + [guid]::NewGuid().ToString('N'))
$project = Join-Path $area 'project'
$addon = Join-Path $project 'addons/crimson-framework'
$data = Join-Path $area 'data'
New-Item -ItemType Directory -Path (Join-Path $addon 'bin'), $data | Out-Null
New-Item -ItemType Directory -Path (Join-Path $project '.godot') | Out-Null
Copy-Item -LiteralPath $binary -Destination (Join-Path $addon 'bin')
$descriptor = @"
; Copyright Francesco Di Blasi 2026 - All Rights Reserved

[configuration]
entry_symbol = "crimson_framework_init"
compatibility_minimum = "4.7"
reloadable = false

[libraries]
windows.x86_64.single = "./bin/$binaryName"
"@
Set-Content -LiteralPath (Join-Path $addon 'crimson-framework.gdextension') -Value $descriptor -Encoding utf8NoBOM
Set-Content -LiteralPath (Join-Path $project 'project.godot') -Value @'
; Copyright Francesco Di Blasi 2026 - All Rights Reserved
config_version=5

[application]
config/name="Crimson Serialization Benchmark"

[rendering]
renderer/rendering_method="gl_compatibility"
'@ -Encoding utf8NoBOM
$fixture = 'network_serialization_benchmark.gd'
Copy-Item -LiteralPath (Join-Path $framework "project/tests/replication/$fixture") -Destination $project
Set-Content -LiteralPath (Join-Path $project '.godot/extension_list.cfg') -Encoding utf8NoBOM -Value 'res://addons/crimson-framework/crimson-framework.gdextension'
Write-Host "Serialization benchmark artifacts: $area"

# Launch a bounded child process and retain both streams before checking its result.
function Invoke-BenchmarkProcess {
    param(
        [string]$Executable,
        [string[]]$Arguments,
        [string]$Name,
        [string]$Marker = ''
    )
    $start = [Diagnostics.ProcessStartInfo]::new($Executable)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.WorkingDirectory = $area
    foreach ($key in @('APPDATA', 'LOCALAPPDATA', 'XDG_DATA_HOME', 'XDG_CONFIG_HOME', 'XDG_CACHE_HOME')) {
        $start.EnvironmentVariables[$key] = $data
    }
    foreach ($argument in $Arguments) { $start.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::Start($start)
    try {
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        $completed = $process.WaitForExit(120000)
        if (!$completed) { $process.Kill($true) }
        $process.WaitForExit()
        $output = $stdout.Result + $stderr.Result
        $log = Join-Path $area "$Name.log"
        Set-Content -LiteralPath $log -Value $output -Encoding utf8NoBOM
        if (!$completed -or $process.ExitCode -ne 0 -or $output -match '(?m)^\s*(SCRIPT ERROR|ERROR):' -or
                ($Marker -and $Marker -cnotin ($output -split '\r?\n'))) {
            throw "$Name failed (exit $($process.ExitCode), completed=$completed). Read $log."
        }
        return $output
    } finally { $process.Dispose() }
}

# Resolve the x64 compiler environment through the installed Visual Studio toolchain.
$locator = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (!(Test-Path -LiteralPath $locator -PathType Leaf)) {
    throw 'Install Visual Studio with the C++ desktop workload to run the native benchmark.'
}
$visualStudio = & $locator -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$visualStudio) { throw 'No Visual Studio installation with the x64 C++ compiler was found.' }
$developerPrompt = Join-Path $visualStudio 'Common7/Tools/VsDevCmd.bat'
$nativeExe = Join-Path $area 'network_serialization_benchmark.exe'
$nativeSource = Join-Path $PSScriptRoot 'fixtures/network_serialization_benchmark.cpp'
$include = Join-Path $framework 'src'
$build = Join-Path $area 'build-native.cmd'
@"
@echo off
rem Copyright Francesco Di Blasi 2026 - All Rights Reserved
call "$developerPrompt" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1
cl.exe /nologo /O2 /DNDEBUG /EHsc /std:c++17 /I"$include" /Fe:"$nativeExe" "$nativeSource"
"@ | Set-Content -LiteralPath $build -Encoding oem
$null = Invoke-BenchmarkProcess $env:ComSpec @('/d', '/c', 'build-native.cmd') 'native-build'
$native = Invoke-BenchmarkProcess $nativeExe @() 'native' 'CRIMSON_NATIVE_CODEC_BENCHMARK_PASS'
$rpcArgs = @('--headless', '--path', $project, '--script', "res://$fixture", '--') + @($Recipients | ForEach-Object { "$_" })
$rpc = Invoke-BenchmarkProcess $engine $rpcArgs 'rpc' 'CRIMSON_NETWORK_RPC_BENCHMARK_PASS'

# Extract machine-readable records without mixing in the engine banner or diagnostics.
$nativeResults = @($native -split '\r?\n' | Where-Object { $_.StartsWith('CRIMSON_NATIVE_CODEC_BENCHMARK ') } |
    ForEach-Object { $_.Substring('CRIMSON_NATIVE_CODEC_BENCHMARK '.Length) | ConvertFrom-Json })
$rpcResults = @($rpc -split '\r?\n' | Where-Object { $_.StartsWith('CRIMSON_NETWORK_RPC_BENCHMARK ') } |
    ForEach-Object { $_.Substring('CRIMSON_NETWORK_RPC_BENCHMARK '.Length) | ConvertFrom-Json })
if ($nativeResults.Count -ne 4 -or $rpcResults.Count -ne $Recipients.Count * 5) {
    throw "The benchmark did not emit the expected result count. Read $area."
}
$resultPath = Join-Path $area 'results.json'
$result = [ordered]@{
    recorded_at_utc = [DateTimeOffset]::UtcNow.ToString('o')
    native_compiler_flags = '/O2 /DNDEBUG /EHsc /std:c++17'
    godot = ($rpc -split '\r?\n')[0]
    cpu = (Get-CimInstance Win32_Processor | Select-Object -First 1 -ExpandProperty Name).Trim()
    release_addon_sha256 = (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash
    native = $nativeResults
    rpc = $rpcResults
}
$result | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $resultPath -Encoding utf8NoBOM
Write-Host $native
Write-Host $rpc
Write-Host "CRIMSON_NETWORK_SERIALIZATION_BENCHMARK_PASS $resultPath"
