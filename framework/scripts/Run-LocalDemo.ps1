#Requires -Version 5.1
# Copyright Francesco Di Blasi 2026 - All Rights Reserved
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('Dedicated', 'Lan')][string]$Mode,
    [ValidateRange(1, 4)][int]$Instances = 4,
    [ValidateRange(1, 65535)][int]$Port = $(if ($Mode -eq 'Dedicated') { 24831 } else { 24830 })
)

$ErrorActionPreference = 'Stop'
$startupTimeoutSeconds = 30

# Finds the installed engine without depending on the caller's working directory.
function Resolve-GodotExecutable {
    if ($env:CRIMSON_GODOT_EXE) {
        return (Resolve-Path -LiteralPath $env:CRIMSON_GODOT_EXE).ProviderPath
    }

    $command = Get-Command -Name 'Godot_v4.7-stable_win64_console.exe', 'godot_console.exe', 'godot.exe' `
        -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) { return $command.Source }

    $desktop = [Environment]::GetFolderPath('DesktopDirectory')
    $candidate = Join-Path $desktop 'GODOT/Godot_v4.7-stable_win64_console.exe'
    if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    throw 'Set CRIMSON_GODOT_EXE to your Godot 4.7 executable or place Godot on PATH.'
}

# Quotes one argument for Windows native parsing, including embedded quotes and trailing backslashes.
function ConvertTo-WindowsProcessArgument {
    param([AllowEmptyString()][string]$Value)

    $escaped = $Value -replace '(\\*)"', '$1$1\"' -replace '(\\+)$', '$1$1'
    return '"' + $escaped + '"'
}

# Starts one owned process with isolated app data and streams its output to per-process logs.
function Start-LocalGodot {
    param([string]$Name, [string[]]$Arguments)

    $data = (New-Item -ItemType Directory -Path (Join-Path $runDirectory "$Name-data")).FullName
    $stdoutPath = Join-Path $runDirectory "$Name.log"
    $stderrPath = Join-Path $runDirectory "$Name-error.log"
    $start = [Diagnostics.ProcessStartInfo]::new($engine)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.EnvironmentVariables['APPDATA'] = $data
    $start.EnvironmentVariables['LOCALAPPDATA'] = $data
    $start.Arguments = ((@('--path', $project) + $Arguments) | ForEach-Object {
        ConvertTo-WindowsProcessArgument $_
    }) -join ' '

    # A one-byte buffer makes startup markers visible to the launcher before process exit.
    $stdout = [IO.FileStream]::new($stdoutPath, 'CreateNew', 'Write', 'Read', 1, $true)
    $stderr = [IO.FileStream]::new($stderrPath, 'CreateNew', 'Write', 'Read', 1, $true)
    try {
        $process = [Diagnostics.Process]::Start($start)
    } catch {
        $stdout.Dispose()
        $stderr.Dispose()
        throw
    }
    $run = @{
        Name = $Name
        Process = $process
        Stdout = $stdout
        Stderr = $stderr
        LogPath = $stdoutPath
        ErrorPath = $stderrPath
        OutputCopy = $process.StandardOutput.BaseStream.CopyToAsync($stdout)
        ErrorCopy = $process.StandardError.BaseStream.CopyToAsync($stderr)
    }
    $runs.Add($run)
    return $run
}

# Closes an owned Godot window where possible and reaps its process tree on timeout.
function Stop-LocalGodot {
    param([hashtable]$Run)

    try {
        if (!$Run.Process.HasExited) {
            if (!$Run.Process.CloseMainWindow() -or !$Run.Process.WaitForExit(1200)) {
                $taskkill = Join-Path $env:SystemRoot 'System32/taskkill.exe'
                Start-Process -FilePath $taskkill -ArgumentList @('/PID', [string]$Run.Process.Id, '/T', '/F') `
                    -WindowStyle Hidden -Wait
                if (!$Run.Process.HasExited) { $Run.Process.Kill() }
                $null = $Run.Process.WaitForExit(5000)
            }
        }
        $null = $Run.OutputCopy.Wait(2000)
        $null = $Run.ErrorCopy.Wait(2000)
    } catch [InvalidOperationException] {
        # The process can exit between checking its state and closing it.
    } finally {
        $Run.Process.Dispose()
        $Run.Stdout.Dispose()
        $Run.Stderr.Dispose()
    }
}

# Reads the role's launch result from its current log, including the expected port.
function Get-StartupSignal {
    param([hashtable]$Run, [string]$Role)

    if ($Run.Process.HasExited) { $null = $Run.OutputCopy.Wait(1000) }
    $lines = Get-Content -LiteralPath $Run.LogPath
    $failurePrefix = "[Crimson] ERROR $Role "
    $failure = $lines | Where-Object { $_.StartsWith($failurePrefix, [StringComparison]::Ordinal) } |
        Select-Object -First 1
    return @{ Ready = "[Crimson] READY $Role $Port" -cin $lines; Error = $failure }
}

# Waits for the authoritative world to open admission and checks that the requested endpoint matches.
function Wait-AuthorityReady {
    param([hashtable]$Run, [string]$Role)

    $deadline = [Diagnostics.Stopwatch]::StartNew()
    while ($deadline.Elapsed.TotalSeconds -lt $startupTimeoutSeconds) {
        $signal = Get-StartupSignal -Run $Run -Role $Role
        if ($signal.Error) { throw "$($signal.Error) See $($Run.LogPath)." }
        if ($Run.Process.HasExited) { break }
        if ($signal.Ready) { return }
        Start-Sleep -Milliseconds 80
    }
    $details = if ($Run.Process.HasExited) { "exited with code $($Run.Process.ExitCode)" } else { 'timed out' }
    throw "The $Role $details before opening admission on UDP $Port. See $($Run.LogPath) and $($Run.ErrorPath)."
}

# Reports each guest's automatic join result while keeping failed guest menus open for a manual retry.
function Wait-GuestStartup {
    param([hashtable]$Authority, [hashtable[]]$Guests)

    $pending = [Collections.Generic.List[hashtable]]::new($Guests)
    $allReady = $true
    $deadline = [Diagnostics.Stopwatch]::StartNew()
    while ($pending.Count -gt 0) {
        for ($index = $pending.Count - 1; $index -ge 0; $index--) {
            $guestRun = $pending[$index]
            $signal = Get-StartupSignal -Run $guestRun -Role 'join'
            $failure = $signal.Error
            if (!$failure -and $signal.Ready) {
                Write-Host "Crimson $($guestRun.Name) ready on UDP $Port."
            } else {
                if (!$failure) {
                    $failure = if ($guestRun.Process.HasExited) {
                        "Exited with code $($guestRun.Process.ExitCode) before joining."
                    } elseif ($Authority.Process.HasExited) {
                        'The authority exited before the guest joined.'
                    } elseif ($deadline.Elapsed.TotalSeconds -ge $startupTimeoutSeconds) {
                        "Timed out waiting to join on UDP $Port."
                    }
                }
                if (!$failure) { continue }
                Write-Warning "Crimson $($guestRun.Name) automatic join failed. $failure See $($guestRun.LogPath) and $($guestRun.ErrorPath)."
                $allReady = $false
            }
            $pending.RemoveAt($index)
        }
        if ($pending.Count -gt 0) { Start-Sleep -Milliseconds 80 }
    }
    return $allReady
}

$framework = Split-Path -Parent $PSScriptRoot
$workspace = Split-Path -Parent $framework
$sourceProject = Join-Path $framework 'project'
$sourceServer = Join-Path $workspace 'server'
$engine = Resolve-GodotExecutable
$binary = Join-Path $sourceProject 'addons/crimson-framework/bin/crimson_framework.windows.template_debug.x86_64.dll'
if (!(Test-Path -LiteralPath $binary -PathType Leaf)) {
    throw "Build the Windows x86-64 template_debug DLL in $framework before running the demo."
}
if ($Mode -eq 'Dedicated' -and !(Test-Path -LiteralPath (Join-Path $sourceServer 'main.tscn') -PathType Leaf)) {
    throw "The dedicated boot scene is missing at $sourceServer/main.tscn."
}

$runDirectory = (New-Item -ItemType Directory -Path (Join-Path ([IO.Path]::GetTempPath()) `
    ('crimson-local-' + [guid]::NewGuid().ToString('N')))).FullName
$project = (New-Item -ItemType Directory -Path (Join-Path $runDirectory 'project')).FullName
$runs = [Collections.Generic.List[hashtable]]::new()

try {
    Write-Host "Run logs and isolated game data: $runDirectory"
    foreach ($entry in @('project.godot', 'addons', 'demo')) {
        Copy-Item -LiteralPath (Join-Path $sourceProject $entry) -Destination $project -Recurse
    }
    if ($Mode -eq 'Dedicated') {
        Copy-Item -LiteralPath $sourceServer -Destination $project -Recurse
    }

    $import = Start-LocalGodot -Name 'import' -Arguments @('--headless', '--editor', '--import')
    if (!$import.Process.WaitForExit(90000) -or $import.Process.ExitCode -ne 0 -or
        !$import.OutputCopy.Wait(3000) -or !$import.ErrorCopy.Wait(3000) -or
        (Select-String -LiteralPath $import.LogPath, $import.ErrorPath -Pattern '^\s*(SCRIPT ERROR|ERROR):' -Quiet)) {
        throw "Godot could not import the staged demo. See $($import.LogPath) and $($import.ErrorPath)."
    }

    $role = if ($Mode -eq 'Dedicated') { 'server' } else { 'listen' }
    $bindAddress = if ($Mode -eq 'Dedicated') { '127.0.0.1' } else { '*' }
    $authorityArguments = if ($Mode -eq 'Dedicated') { @('--headless', 'res://server/main.tscn') } else { @() }
    $authority = Start-LocalGodot -Name $role -Arguments ($authorityArguments + @('--',
        "--crimson-role=$role", "--crimson-bind=$bindAddress", "--crimson-port=$Port"))
    Wait-AuthorityReady -Run $authority -Role $role
    Write-Host "Crimson $role ready on UDP $Port."

    $guestCount = if ($Mode -eq 'Dedicated') { 1 } else { $Instances - 1 }
    $guests = [Collections.Generic.List[hashtable]]::new()
    $guest = $null
    for ($number = 1; $number -le $guestCount; $number++) {
        $guest = Start-LocalGodot -Name "join-$number" -Arguments @('--', '--crimson-role=join',
            '--crimson-address=127.0.0.1', "--crimson-port=$Port")
        $guests.Add($guest)
    }
    $guestsReady = Wait-GuestStartup -Authority $authority -Guests $guests.ToArray()

    # In dedicated mode the visible client's lifetime owns its paired headless server.
    while (!$authority.Process.HasExited -and ($Mode -eq 'Lan' -or !$guest.Process.HasExited)) {
        Start-Sleep -Milliseconds 150
    }
    if ($authority.Process.HasExited -and $authority.Process.ExitCode -ne 0) {
        throw "Crimson $role exited with code $($authority.Process.ExitCode). See $($authority.ErrorPath)."
    }
    if ($Mode -eq 'Dedicated' -and $guest.Process.HasExited -and $guest.Process.ExitCode -ne 0) {
        throw "The dedicated client exited with code $($guest.Process.ExitCode). See $($guest.ErrorPath)."
    }
    if (!$guestsReady) { throw "One or more automatic joins failed. See the logs in $runDirectory." }
} finally {
    for ($index = $runs.Count - 1; $index -ge 0; $index--) {
        Stop-LocalGodot -Run $runs[$index]
    }
}
