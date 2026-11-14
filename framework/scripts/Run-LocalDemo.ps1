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
$explicitPort = $PSBoundParameters.ContainsKey('Port')

# Checks a UDP bind in the launcher's address scope. Port zero asks Windows for a free port.
function Get-BindableUdpPort {
    param([int]$RequestedPort, [string]$BindAddress)

    $wildcard = $BindAddress -eq '*'
    $dualStack = $wildcard -and [Net.Sockets.Socket]::OSSupportsIPv6
    $address = if ($dualStack) { [Net.IPAddress]::IPv6Any }
        elseif ($wildcard) { [Net.IPAddress]::Any }
        else { [Net.IPAddress]::Parse($BindAddress) }
    $socket = [Net.Sockets.Socket]::new($address.AddressFamily, [Net.Sockets.SocketType]::Dgram,
        [Net.Sockets.ProtocolType]::Udp)
    try {
        $socket.ExclusiveAddressUse = $true
        if ($dualStack) { $socket.DualMode = $true }
        $socket.Bind([Net.IPEndPoint]::new($address, $RequestedPort))
        return $socket.LocalEndPoint.Port
    } finally {
        $socket.Dispose()
    }
}

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

# Measures the primary work area and title-bar insets in native pixels for Godot's launch flags.
function Initialize-LanWindowLayout {
    if ('CrimsonLauncher.LanWindowLayout' -as [type]) { return }
    Add-Type -TypeDefinition @'
// Copyright Francesco Di Blasi 2026 - All Rights Reserved
using System;
using System.Runtime.InteropServices;

namespace CrimsonLauncher
{
    public static class LanWindowLayout
    {
        // Native screen rectangle, with an exclusive bottom-right corner.
        [StructLayout(LayoutKind.Sequential)]
        private struct Rect { public int Left, Top, Right, Bottom; }

        // Receives the physical rectangle of one display during enumeration.
        private delegate bool MonitorCallback(IntPtr monitor, IntPtr context, ref Rect bounds, IntPtr data);

        // Read the primary work area, which excludes the Windows taskbar and docked toolbars.
        [DllImport("user32.dll", EntryPoint = "SystemParametersInfoW", SetLastError = true)]
        private static extern bool SystemParametersInfo(int action, int parameter, out Rect rect, int flags);
        // Enumerate monitor rectangles in the same coordinate system Godot uses.
        [DllImport("user32.dll")]
        private static extern bool EnumDisplayMonitors(IntPtr context, IntPtr clip, MonitorCallback callback, IntPtr data);
        // Read the primary display DPI in the current DPI context.
        [DllImport("user32.dll")]
        private static extern uint GetDpiForSystem();
        // Use physical-pixel coordinates while measuring the layout, then restore the caller's context.
        [DllImport("user32.dll")]
        private static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
        // Measure the standard resizable Godot window's frame around a zero-sized client rectangle.
        [DllImport("user32.dll", SetLastError = true)]
        private static extern bool AdjustWindowRectExForDpi(ref Rect rect, uint style, bool menu, uint extendedStyle, uint dpi);

        // Return Godot's client rectangles for all slots, in row order beginning with the host.
        public static string[][] Arguments(int count)
        {
            const int getWorkArea = 0x0030;
            const uint overlappedWindow = 0x00CF0000;
            const uint windowEdge = 0x00000100;
            IntPtr previous = SetThreadDpiAwarenessContext(new IntPtr(-4)); // Per-monitor DPI aware v2.
            if (previous == IntPtr.Zero)
                previous = SetThreadDpiAwarenessContext(new IntPtr(-3)); // Windows 10 1607 supports v1.
            if (previous == IntPtr.Zero)
                throw new InvalidOperationException("Could not select the Windows monitor DPI context.");

            try
            {
                Rect area;
                Rect frame = new Rect();
                if (!SystemParametersInfo(getWorkArea, 0, out area, 0) ||
                    !AdjustWindowRectExForDpi(ref frame, overlappedWindow, false, windowEdge, GetDpiForSystem()))
                    throw new InvalidOperationException("Could not measure the primary display or the game window frame.");

                int originX = 0;
                int originY = 0;
                MonitorCallback collectOrigin = delegate(IntPtr monitor, IntPtr context, ref Rect bounds, IntPtr data)
                {
                    originX = Math.Min(originX, bounds.Left);
                    originY = Math.Min(originY, bounds.Top);
                    return true;
                };
                if (!EnumDisplayMonitors(IntPtr.Zero, IntPtr.Zero, collectOrigin, IntPtr.Zero))
                    throw new InvalidOperationException("Could not read the virtual desktop origin.");

                // Two windows share a row. Three or four use a 2x2 grid.
                int rows = count > 2 ? 2 : 1;
                int workWidth = area.Right - area.Left;
                int workHeight = area.Bottom - area.Top;
                string[][] slots = new string[count][];
                for (int index = 0; index < count; index++)
                {
                    int column = index % 2;
                    int row = index / 2;
                    int left = workWidth * column / 2;
                    int top = workHeight * row / rows;
                    int width = workWidth * (column + 1) / 2 - left - (frame.Right - frame.Left);
                    int height = workHeight * (row + 1) / rows - top - (frame.Bottom - frame.Top);
                    if (width <= 0 || height <= 0)
                        throw new InvalidOperationException("The primary display has no room for the requested LAN windows.");

                    // Godot's position is the client origin relative to the virtual desktop's top-left.
                    int x = area.Left + left - frame.Left - originX;
                    int y = area.Top + top - frame.Top - originY;
                    slots[index] = new[] { "--windowed", "--position", x + "," + y, "--resolution", width + "x" + height };
                }
                return slots;
            }
            finally
            {
                SetThreadDpiAwarenessContext(previous);
            }
        }
    }
}
'@
}

# Compute all slot arguments before starting any process so a layout failure does not split the launch.
function Get-LanWindowLayout {
    param([int]$Count)

    $layout = @{}
    if ($Count -gt 1) {
        try {
            Initialize-LanWindowLayout
            $slots = [CrimsonLauncher.LanWindowLayout]::Arguments($Count)
            for ($index = 0; $index -lt $Count; $index++) {
                $layout[$index] = $slots[$index]
            }
        } catch {
            Write-Warning "Could not tile LAN windows. Using the game's default window size and position. $($_.Exception.Message)"
            $layout.Clear()
        }
    }
    return $layout
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
    if (!$start.EnvironmentVariables['CRIMSON_NETWORK_PROFILE_STORE']) {
        $start.EnvironmentVariables['CRIMSON_NETWORK_PROFILE_STORE'] = $networkProfileStore
    }
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
    param([hashtable]$Run, [string]$Role, [string]$BindAddress)

    $deadline = [Diagnostics.Stopwatch]::StartNew()
    while ($deadline.Elapsed.TotalSeconds -lt $startupTimeoutSeconds) {
        $signal = Get-StartupSignal -Run $Run -Role $Role
        if ($signal.Error) {
            throw "$($signal.Error) Endpoint $BindAddress on UDP $Port. See $($Run.ErrorPath) and $($Run.LogPath)."
        }
        if ($Run.Process.HasExited) { break }
        if ($signal.Ready) { return }
        Start-Sleep -Milliseconds 80
    }
    $details = if ($Run.Process.HasExited) { "exited with code $($Run.Process.ExitCode)" } else { 'timed out' }
    throw "The $Role $details before opening admission on UDP $Port. See $($Run.LogPath) and $($Run.ErrorPath)."
}

# Stops the run on a failed child or when a required process closes during startup.
function Assert-LocalProcessHealthy {
    param([string]$Phase, [hashtable[]]$Required = @())

    foreach ($run in $runs) {
        if ($run.Process.HasExited -and ($run.Process.ExitCode -ne 0 -or $run -in $Required)) {
            $code = $run.Process.ExitCode
            $hexCode = '0x{0:X8}' -f $code
            throw "Crimson $($run.Name) exited during $Phase with code $code ($hexCode). See $($run.LogPath) and $($run.ErrorPath)."
        }
    }
}

# Waits for one guest before starting the next. A live join rejection keeps its menu open for retry.
function Wait-GuestStartup {
    param([hashtable]$Authority, [hashtable]$Guest)

    $deadline = [Diagnostics.Stopwatch]::StartNew()
    while ($deadline.Elapsed.TotalSeconds -lt $startupTimeoutSeconds) {
        Assert-LocalProcessHealthy -Phase 'startup' -Required @($Authority, $Guest)
        $signal = Get-StartupSignal -Run $Guest -Role 'join'
        if ($signal.Error) {
            Write-Warning "$($signal.Error) See $($Guest.LogPath) and $($Guest.ErrorPath). Retry from the guest's Join Game menu."
            return 'Rejected'
        }
        if ($signal.Ready) {
            Write-Host "Crimson $($Guest.Name) ready on UDP $Port."
            return 'Ready'
        }
        Start-Sleep -Milliseconds 80
    }
    Write-Warning "Crimson $($Guest.Name) timed out waiting to join UDP $Port. No further guests will start. See $($Guest.LogPath) and $($Guest.ErrorPath)."
    return 'TimedOut'
}

$bindAddress = if ($Mode -eq 'Dedicated') { '127.0.0.1' } else { '*' }

$framework = Split-Path -Parent $PSScriptRoot
$workspace = Split-Path -Parent $framework
$sourceProject = Join-Path $framework 'project'
$sourceServer = Join-Path $workspace 'server'
$networkProfileStore = Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'Crimson Demo/Network Profiles'
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

    $lanRenderArguments = @()
    if ($Mode -eq 'Lan' -and $Instances -gt 1) {
        # Shader compilation uses Godot's worker pool. Bound it in the staged project only.
        Set-Content -LiteralPath (Join-Path $project 'override.cfg') -Encoding ASCII -Value @(
            '[threading]', 'worker_pool/max_threads=4')
        $lanRenderArguments = @('--max-fps', '60')
    }

    $import = Start-LocalGodot -Name 'import' -Arguments @('--headless', '--editor', '--import', '--frame-delay', '1000')
    if (!$import.Process.WaitForExit(90000) -or $import.Process.ExitCode -ne 0 -or
        !$import.OutputCopy.Wait(3000) -or !$import.ErrorCopy.Wait(3000) -or
        (Select-String -LiteralPath $import.LogPath, $import.ErrorPath -Pattern '^\s*(SCRIPT ERROR|ERROR):' -Quiet)) {
        throw "Godot could not import the staged demo. See $($import.LogPath) and $($import.ErrorPath)."
    }

    $role = if ($Mode -eq 'Dedicated') { 'server' } else { 'listen' }
    try {
        $Port = Get-BindableUdpPort -RequestedPort $Port -BindAddress $bindAddress
    } catch [Net.Sockets.SocketException] {
        $reason = $_.Exception.SocketErrorCode
        if ($reason -notin @([Net.Sockets.SocketError]::AddressAlreadyInUse, [Net.Sockets.SocketError]::AccessDenied)) {
            throw
        }
        $unavailablePort = $Port
        if ($explicitPort) {
            $retry = if ($Mode -eq 'Lan') { "Run-LAN.bat $Instances" } else { 'Run-Dedicated.bat' }
            throw "UDP port $unavailablePort is unavailable on $bindAddress ($reason). Run $retry without an explicit port to select an available one."
        }
        $Port = Get-BindableUdpPort -RequestedPort 0 -BindAddress $bindAddress
        Write-Host "Crimson default UDP port $unavailablePort is unavailable. This run will use UDP $Port."
    }
    $windowLayout = if ($Mode -eq 'Lan') { Get-LanWindowLayout -Count $Instances } else { @{} }
    $authorityArguments = @()
    if ($Mode -eq 'Dedicated') {
        $authorityArguments = @('--headless', 'res://server/main.tscn')
    } else {
        $authorityArguments = $lanRenderArguments
        if ($windowLayout.ContainsKey(0)) { $authorityArguments += $windowLayout[0] }
    }
    $authority = Start-LocalGodot -Name $role -Arguments ($authorityArguments + @('--',
        "--crimson-role=$role", "--crimson-bind=$bindAddress", "--crimson-port=$Port"))
    Wait-AuthorityReady -Run $authority -Role $role -BindAddress $bindAddress
    Write-Host "Crimson $role ready on UDP $Port."

    $guestCount = if ($Mode -eq 'Dedicated') { 1 } else { $Instances - 1 }
    $guestsReady = $true
    $guest = $null
    for ($number = 1; $number -le $guestCount; $number++) {
        Assert-LocalProcessHealthy -Phase 'startup' -Required @($authority)
        $guestArguments = if ($windowLayout.ContainsKey($number)) { $windowLayout[$number] } else { @() }
        $guest = Start-LocalGodot -Name "join-$number" -Arguments ($lanRenderArguments + @($guestArguments) + @('--', '--crimson-role=join',
            '--crimson-address=127.0.0.1', "--crimson-port=$Port"))
        $guestResult = Wait-GuestStartup -Authority $authority -Guest $guest
        if ($guestResult -ne 'Ready') { $guestsReady = $false }
        if ($guestResult -eq 'TimedOut') { break }
    }

    # In dedicated mode the visible client's lifetime owns its paired headless server.
    while (!$authority.Process.HasExited -and ($Mode -eq 'Lan' -or !$guest.Process.HasExited)) {
        Assert-LocalProcessHealthy -Phase 'the session'
        Start-Sleep -Milliseconds 150
    }
    Assert-LocalProcessHealthy -Phase 'the session'
    if (!$guestsReady) { throw "One or more automatic joins failed. See the logs in $runDirectory." }
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
} finally {
    for ($index = $runs.Count - 1; $index -ge 0; $index--) {
        Stop-LocalGodot -Run $runs[$index]
    }
}
