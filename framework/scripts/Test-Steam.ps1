#Requires -Version 7.0
# Copyright Francesco Di Blasi 2026 - All Rights Reserved

[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$GodotExe,
    [ValidateSet('all', 'client', 'apps', 'utils', 'diagnostics', 'http', 'stats', 'achievement_preview', 'cloud', 'timeline', 'overlay',
        'screenshots', 'remote_play', 'inventory', 'workshop', 'presence', 'device',
        'keyboard', 'music', 'video', 'lobbies', 'auth', 'input', 'browser', 'purchases', 'voice', 'parties',
        'server_browser', 'messages', 'sockets', 'publisher', 'publisher_notifications', 'moderation', 'publisher_purchases',
        'publisher_ownership', 'publisher_inventory', 'inventory_verifier', 'server_progression')][string]$Case = 'client',
    [ValidateSet('spacewar', 'app', 'missing', 'sdk-off', 'mock', 'invalid', 'offline', 'runtime-mock')][string]$Mode = 'missing',
    [ValidateSet('template_debug', 'template_release')][string[]]$Target = @('template_debug', 'template_release'),
    [string]$BinaryDirectory,
    [uri]$TrustedHttpsUrl,
    [uint32]$AppId,
    [string]$Achievement
)

$ErrorActionPreference = 'Stop'
$framework = Split-Path -Parent $PSScriptRoot
$engine = (Resolve-Path -LiteralPath $GodotExe).ProviderPath
$publisherCases = @('publisher', 'publisher_notifications', 'moderation', 'publisher_purchases', 'publisher_inventory')
if ($Mode -in @('offline', 'runtime-mock') -and ($Case -ne 'client' -or ($Mode -eq 'runtime-mock' -and !$BinaryDirectory))) {
    throw 'Offline and runtime-mock are client fixtures. Supply an isolated test DLL directory for runtime-mock.'
}
if ($Case -eq 'server_progression' -and $Mode -notin @('missing', 'sdk-off')) {
    throw 'Use missing or sdk-off for the detached fixture, or the dedicated host and guest for live progression.'
}
if ($Case -eq 'achievement_preview') {
    if ($Mode -notin @('spacewar', 'app')) { throw 'The achievement preview requires spacewar or app mode.' }
    if (!$PSBoundParameters.ContainsKey('Target')) { $Target = @('template_debug') }
    if ($Target.Count -ne 1) { throw 'Select one -Target for the one-time achievement unlock.' }
    if ($Mode -eq 'spacewar') {
        if ($PSBoundParameters.ContainsKey('AppId') -or ($Achievement -and $Achievement -cne 'ACH_WIN_ONE_GAME')) {
            throw 'The Spacewar preview selects AppID 480 and the built-in Winner achievement.'
        }
        $Achievement = 'ACH_WIN_ONE_GAME'
    } elseif (!$AppId -or $AppId -eq 480 -or [string]::IsNullOrWhiteSpace($Achievement)) {
        throw 'Supply your authorized -AppId and the published client-set API name in -Achievement.'
    }
} elseif ($PSBoundParameters.ContainsKey('AppId') -or $PSBoundParameters.ContainsKey('Achievement')) {
    throw '-AppId and -Achievement require -Case achievement_preview.'
}
if ($Case -eq 'all') {
    if ($Mode -notin @('spacewar', 'missing', 'sdk-off', 'mock')) {
        throw '-Case all supports spacewar, missing, sdk-off, or mock mode.'
    }
    $cases = if ($Mode -eq 'mock') {
        if (!$BinaryDirectory) { throw 'The mock suite requires -BinaryDirectory pointing to an isolated trusted test build.' }
        $publisherCases + 'publisher_ownership'
    } else {
        @('client', 'apps', 'utils', 'diagnostics', 'http', 'stats', 'cloud', 'timeline', 'overlay',
            'screenshots', 'remote_play', 'inventory', 'workshop', 'presence', 'device', 'keyboard', 'music',
            'video', 'lobbies', 'auth', 'input', 'browser', 'purchases', 'voice', 'parties', 'server_browser',
            'messages', 'sockets')
        if ($Mode -ne 'spacewar') { 'inventory_verifier'; 'server_progression' }
    }
    $failures = [Collections.Generic.List[string]]::new()
    foreach ($feature in $cases) {
        $fixture = @{ GodotExe = $engine; Case = $feature; Mode = $Mode; Target = $Target }
        if ($BinaryDirectory) { $fixture.BinaryDirectory = $BinaryDirectory }
        if ($feature -eq 'http' -and $TrustedHttpsUrl) { $fixture.TrustedHttpsUrl = $TrustedHttpsUrl }
        try {
            & $PSCommandPath @fixture
        } catch {
            $failures.Add($feature)
            Write-Warning "Steam $feature failed. $($_.Exception.Message)"
        }
    }
    if ($failures.Count -gt 0) {
        throw "Steam $Mode suite failed $($failures.Count) of $($cases.Count) cases: $($failures -join ', ')."
    }
    Write-Host "Steam $Mode suite passed all $($cases.Count) cases."
    return
}
if ($Case -eq 'publisher_ownership') {
    if ($Mode -notin @('mock', 'sdk-off', 'missing', 'invalid', 'app') -or !$BinaryDirectory) {
        throw 'The publisher ownership fixture requires -BinaryDirectory pointing to an isolated trusted build.'
    }
    if ($Mode -eq 'app' -and $Target.Count -ne 1) {
        throw 'Select one -Target per live ownership run because Steam rate-limits encrypted ticket requests.'
    }
} elseif (($Case -in $publisherCases) -ne ($Mode -eq 'mock') -or ($Mode -eq 'mock' -and !$BinaryDirectory) -or
        $Mode -eq 'invalid') {
    throw 'Publisher fixtures require -Mode mock and -BinaryDirectory pointing to a trusted test build.'
}
if ($TrustedHttpsUrl -and ($Case -ne 'http' -or $Mode -ne 'spacewar' -or
        !$TrustedHttpsUrl.IsAbsoluteUri -or $TrustedHttpsUrl.Scheme -cne 'https' -or $TrustedHttpsUrl.UserInfo)) {
    throw '-TrustedHttpsUrl requires the signed HTTP fixture and an absolute HTTPS URL without credentials.'
}
$steamAppId = if ($Mode -in @('spacewar', 'offline', 'runtime-mock')) { '480' }
    elseif ($Case -eq 'achievement_preview') { [string]$AppId } else { '' }
if ($Mode -eq 'app' -and $Case -ne 'achievement_preview') {
    $appVariable = switch ($Case) {
        'stats' {
            if ($env:CRIMSON_STEAM_STATS_ALLOW_WRITES -cne '1') {
                throw 'App mode for stats requires CRIMSON_STEAM_STATS_ALLOW_WRITES=1.'
            }
            'CRIMSON_STEAM_STATS_APP_ID'
        }
        'apps' { 'CRIMSON_STEAM_APPS_APP_ID' }
        'inventory' { 'CRIMSON_STEAM_INVENTORY_APP_ID' }
        'workshop' { 'CRIMSON_STEAM_WORKSHOP_APP_ID' }
        'presence' {
            if ($env:CRIMSON_STEAM_PRESENCE_ALLOW_WRITES -cne '1') {
                throw 'App mode for presence requires CRIMSON_STEAM_PRESENCE_ALLOW_WRITES=1.'
            }
            'CRIMSON_STEAM_PRESENCE_APP_ID'
        }
        'cloud' {
            if ($env:CRIMSON_STEAM_CLOUD_ALLOW_SHARE -cne '1') {
                throw 'App mode for Cloud sharing requires CRIMSON_STEAM_CLOUD_ALLOW_SHARE=1.'
            }
            'CRIMSON_STEAM_CLOUD_APP_ID'
        }
        'publisher_ownership' {
            if ($env:CRIMSON_STEAM_OWNERSHIP_ALLOW_LIVE -cne '1' -or !$env:CRIMSON_STEAM_OWNERSHIP_KEY_FILE) {
                throw 'App mode for ownership requires CRIMSON_STEAM_OWNERSHIP_ALLOW_LIVE=1 and CRIMSON_STEAM_OWNERSHIP_KEY_FILE.'
            }
            'CRIMSON_STEAM_OWNERSHIP_APP_ID'
        }
        default { throw "App mode is unavailable for the $Case case." }
    }
    $configuredId = [Environment]::GetEnvironmentVariable($appVariable, 'Process')
    [uint32]$parsedId = 0
    if ($configuredId -cnotmatch '^[1-9][0-9]{0,9}$' -or
        ![uint32]::TryParse($configuredId, [ref]$parsedId) -or $parsedId -eq 480) {
        throw "App mode requires an authorized non-Spacewar AppID in $appVariable."
    }
    $steamAppId = [string]$parsedId
}
$sourceAddon = Join-Path $framework 'project/addons/crimson-framework'
$binarySource = if ($BinaryDirectory) { (Resolve-Path -LiteralPath $BinaryDirectory).ProviderPath }
    else { Join-Path $sourceAddon 'bin' }
$work = (New-Item -ItemType Directory -Path (Join-Path ([IO.Path]::GetTempPath()) (
    "crimson-steam-$Case-" + [guid]::NewGuid().ToString('N')))).FullName
Write-Host "Steam $Case verification logs $work"

# Start one owned Godot child with local app data and a bounded exit.
function Invoke-SteamFixture {
    param([string]$Directory, [string]$LogName, [string[]]$Arguments, [string]$Marker = '', [switch]$Rendered)

    $data = (New-Item -ItemType Directory -Path (Join-Path $Directory "$LogName-data")).FullName
    $start = [Diagnostics.ProcessStartInfo]::new($testEngine)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.WorkingDirectory = $engineDirectory
    $start.Environment['APPDATA'] = $data
    $start.Environment['LOCALAPPDATA'] = $data
    if ($steamAppId) {
        $start.Environment['SteamAppId'] = $steamAppId
        $start.Environment['SteamGameId'] = $steamAppId
    }
    if ($Case -eq 'achievement_preview' -and $Rendered) {
        $start.Environment['CRIMSON_STEAM_STATS_ALLOW_WRITES'] = '1'
    }
    if (!$Rendered) {
        $start.ArgumentList.Add('--headless')
    }
    $project = Join-Path $Directory 'project'
    foreach ($argument in @('--path', $project) + $Arguments) {
        $start.ArgumentList.Add($argument)
    }
    $process = [Diagnostics.Process]::Start($start)
    $log = Join-Path $Directory "$LogName.log"
    try {
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        $timeout = if ($Rendered) {
            180000
        } elseif ($Case -eq 'workshop' -and $Mode -eq 'app' -and $LogName -eq 'workshop') {
            300000
        } else {
            45000
        }
        if (!$process.WaitForExit($timeout) -or !$stdout.Wait(3000) -or !$stderr.Wait(3000)) {
            throw "Godot fixture timed out. Evidence directory $Directory."
        }
        $output = (@($stdout.Result, $stderr.Result) -join [Environment]::NewLine) -replace
            '(?<![0-9])7656[0-9]{13}(?![0-9])', '[SteamID redacted]'
        [IO.File]::WriteAllText($log, $output)
        if ($process.ExitCode -ne 0 -or $output -match '(?m)^\s*(SCRIPT ERROR|ERROR):') {
            throw "Godot fixture failed with exit $($process.ExitCode). See $log."
        }
        if ($Marker) {
            if ($Marker -cnotin ($output -split '\r?\n')) { throw "Missing $Marker. See $log." }
            Write-Host $Marker
        }
    } finally {
        if (!$process.HasExited) {
            $process.Kill($true)
            [void]$process.WaitForExit(5000)
        }
        $process.Dispose()
    }
}

$engineDirectory = (New-Item -ItemType Directory -Path (Join-Path $work 'engine')).FullName
Copy-Item -LiteralPath $engine -Destination $engineDirectory
$testEngine = Join-Path $engineDirectory ([IO.Path]::GetFileName($engine))
if ($engine -match '_console\.exe$') {
    Copy-Item -LiteralPath ($engine -replace '_console\.exe$', '.exe') -Destination $engineDirectory
}
if ($steamAppId) {
    Set-Content -LiteralPath (Join-Path $engineDirectory 'steam_appid.txt') -Value $steamAppId -Encoding ascii
}

$server = $null
$certificatePath = $null
$keyPath = $null
try {
    $fixtureArguments = @($Mode)
    if ($Case -eq 'achievement_preview') {
        $fixtureArguments = @($steamAppId, $Achievement, "--crimson-steam-overlay=$steamAppId")
    }
    if ($Case -eq 'workshop' -and $Mode -eq 'app' -and $env:CRIMSON_STEAM_WORKSHOP_SHOW_EULA -ceq '1') {
        $fixtureArguments += "--crimson-steam-overlay=$steamAppId"
    }
    if ($Case -eq 'diagnostics' -and $Mode -eq 'spacewar') {
        $fixtureArguments += '-debug_steamapi'
    }
    if ($Case -eq 'http' -and $Mode -eq 'spacewar') {
        $python = Get-Command python -CommandType Application -ErrorAction Stop | Select-Object -First 1
        $certificatePath = Join-Path $work 'loopback.pem'
        $keyPath = Join-Path $work 'loopback-key.pem'
        $key = [Security.Cryptography.RSA]::Create(2048)
        try {
            $request = [Security.Cryptography.X509Certificates.CertificateRequest]::new(
                'CN=127.0.0.1', $key, [Security.Cryptography.HashAlgorithmName]::SHA256,
                [Security.Cryptography.RSASignaturePadding]::Pkcs1)
            $names = [Security.Cryptography.X509Certificates.SubjectAlternativeNameBuilder]::new()
            $names.AddIpAddress([Net.IPAddress]::Loopback)
            [void]$request.CertificateExtensions.Add($names.Build())
            $certificate = $request.CreateSelfSigned([DateTimeOffset]::UtcNow.AddMinutes(-1),
                [DateTimeOffset]::UtcNow.AddHours(1))
            try {
                [IO.File]::WriteAllText($certificatePath, $certificate.ExportCertificatePem())
                [IO.File]::WriteAllText($keyPath, $key.ExportPkcs8PrivateKeyPem())
            } finally {
                $certificate.Dispose()
            }
        } finally {
            $key.Dispose()
        }
        $start = [Diagnostics.ProcessStartInfo]::new($python.Source)
        $start.UseShellExecute = $false
        $start.CreateNoWindow = $true
        $start.RedirectStandardOutput = $true
        $start.ArgumentList.Add((Join-Path $PSScriptRoot 'fixtures/steam_http_server.py'))
        foreach ($argument in @('--tls-cert', $certificatePath, '--tls-key', $keyPath)) {
            $start.ArgumentList.Add($argument)
        }
        $server = [Diagnostics.Process]::Start($start)
        $listen = $server.StandardOutput.ReadLineAsync()
        if (!$listen.Wait(5000)) {
            throw 'The Steam HTTP loopback fixture did not publish its port.'
        }
        $port = [regex]::Match([string]$listen.Result, '^CRIMSON_STEAM_HTTP_PORT=([0-9]{1,5})$')
        if (!$port.Success) {
            throw 'The Steam HTTP loopback fixture did not publish its port.'
        }
        $fixtureArguments += "http://127.0.0.1:$($port.Groups[1].Value)"
        $listen = $server.StandardOutput.ReadLineAsync()
        if (!$listen.Wait(5000)) {
            throw 'The Steam HTTPS loopback fixture did not publish its port.'
        }
        $securePort = [regex]::Match([string]$listen.Result, '^CRIMSON_STEAM_HTTPS_PORT=([0-9]{1,5})$')
        if (!$securePort.Success) {
            throw 'The Steam HTTPS loopback fixture did not publish its port.'
        }
        $fixtureArguments += "https://127.0.0.1:$($securePort.Groups[1].Value)"
        if ($TrustedHttpsUrl) {
            $fixtureArguments += $TrustedHttpsUrl.AbsoluteUri
        }
    }

    $fixture = "steam_$Case.gd"
    $fixtureDirectory = if ($Case -in $publisherCases -or $Case -eq 'publisher_ownership') {
        Join-Path $PSScriptRoot 'fixtures/steam_publisher'
    } else {
        Join-Path $framework 'project/tests/online'
    }
    foreach ($variant in $Target) {
        $directory = Join-Path $work $variant
        $project = Join-Path $directory 'project'
        $addon = Join-Path $project 'addons/crimson-framework'
        $binary = "crimson_framework.windows.$variant.x86_64.dll"
        $bin = (New-Item -ItemType Directory -Path (Join-Path $addon 'bin') -Force).FullName
        Copy-Item -LiteralPath (Join-Path $binarySource $binary) -Destination $bin
        if ($steamAppId) {
            Copy-Item -LiteralPath (Join-Path $binarySource 'steam_api64.dll') -Destination $bin
        }
        if ($Case -eq 'publisher_ownership' -and $Mode -in @('invalid', 'app')) {
            Copy-Item -LiteralPath (Join-Path $binarySource 'sdkencryptedappticket64.dll') -Destination $bin
        }
        $descriptor = Get-Content -LiteralPath (Join-Path $sourceAddon 'crimson-framework.gdextension') -Raw
        if ($variant -eq 'template_release') { $descriptor = $descriptor.Replace('.template_debug.', '.template_release.') }
        $descriptor = [regex]::Replace($descriptor, '(?ms)^\[icons\].*?(?=^\[|\z)', '')
        Set-Content -LiteralPath (Join-Path $addon 'crimson-framework.gdextension') -Value $descriptor -Encoding utf8NoBOM
        @'
config_version=5
[application]
config/name="Crimson Steam Client Fixture"
'@ | Set-Content -LiteralPath (Join-Path $project 'project.godot') -Encoding utf8NoBOM
        Copy-Item -LiteralPath (Join-Path $fixtureDirectory $fixture) -Destination $project
        if ($Case -in @('parties', 'messages')) {
            $onlineFixtures = (New-Item -ItemType Directory -Path (Join-Path $project 'tests/online')).FullName
            Copy-Item -LiteralPath (Join-Path $fixtureDirectory 'steam_game_fixture.gd') -Destination $onlineFixtures
        }

        Write-Host "Testing $variant ($Mode)"
        $godotMetadata = Join-Path $project '.godot'
        New-Item -ItemType Directory -Path $godotMetadata | Out-Null
        Set-Content -LiteralPath (Join-Path $godotMetadata 'extension_list.cfg') -Encoding utf8NoBOM -Value 'res://addons/crimson-framework/crimson-framework.gdextension'
        $marker = 'CRIMSON_STEAM_{0}_PASSED {1}' -f $Case.ToUpperInvariant(), $Mode
        $arguments = @('--script', "res://$fixture", '--') + $fixtureArguments
        $rendered = $Case -eq 'achievement_preview' -or
            ($Case -eq 'workshop' -and $Mode -eq 'app' -and $env:CRIMSON_STEAM_WORKSHOP_SHOW_EULA -ceq '1')
        if ($Case -eq 'achievement_preview') { Write-Host 'Use the achievement preview window to unlock once, observe Steam, and close the window.' }
        Invoke-SteamFixture -Directory $directory -LogName $Case -Arguments $arguments -Marker $marker -Rendered:$rendered
    }
} finally {
    if ($server) {
        if (!$server.HasExited) {
            $server.Kill($true)
            [void]$server.WaitForExit(5000)
        }
        $server.Dispose()
    }
    if ($certificatePath) {
        [IO.File]::Delete($certificatePath)
    }
    if ($keyPath) {
        [IO.File]::Delete($keyPath)
    }
}
