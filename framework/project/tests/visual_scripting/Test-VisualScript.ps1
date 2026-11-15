#Requires -Version 7.0
# Copyright Francesco Di Blasi 2026 - All Rights Reserved
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$GodotExe,
    [ValidateSet('template_debug', 'template_release')][string[]]$Target = @('template_debug', 'template_release'),
    [string]$BinaryDirectory,
    [switch]$VerifyPack,
    [switch]$VerifyEditor,
    [switch]$VerifyDebugger,
    [switch]$VerifyNativeFocus
)

$ErrorActionPreference = 'Stop'
if ($VerifyNativeFocus -and 'template_debug' -notin $Target) {
    throw '-VerifyNativeFocus requires template_debug.'
}
$engine = (Resolve-Path -LiteralPath $GodotExe).ProviderPath
$sourceProject = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).ProviderPath
$sourceAddon = Join-Path $sourceProject 'addons/crimson-framework'
$pinnedApi = (Resolve-Path -LiteralPath (Join-Path $sourceProject '../godot-cpp/gdextension/extension_api-4-7.json')).ProviderPath
if (!$BinaryDirectory) { $BinaryDirectory = Join-Path $sourceAddon 'bin' }
$binarySource = (Resolve-Path -LiteralPath $BinaryDirectory).ProviderPath
$descriptor = Get-Content -Raw -LiteralPath (Join-Path $sourceAddon 'crimson-framework.gdextension')

# Run one Godot phase with isolated application data and a retained combined log.
function Invoke-VisualScriptGodot {
    param(
        [string]$Project,
        [string]$Area,
        [string]$Phase,
        [string[]]$Arguments,
        [switch]$NativeWindow,
        [string]$ExpectedErrors
    )

    $data = Join-Path $Area 'data'
    New-Item -ItemType Directory -Path $data -Force | Out-Null
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $engine
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.EnvironmentVariables['APPDATA'] = $data
    $start.EnvironmentVariables['LOCALAPPDATA'] = $data
    $displayArguments = if ($NativeWindow) { @() } else { @('--headless') }
    foreach ($argument in (@($displayArguments) + @('--path', $Project) + $Arguments)) {
        $start.ArgumentList.Add($argument)
    }

    $log = Join-Path $Area "$Phase.log"
    $process = [Diagnostics.Process]::Start($start)
    try {
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (!$process.WaitForExit(30000) -or !$stdout.Wait(3000) -or !$stderr.Wait(3000)) {
            throw "Godot did not complete $Phase within the time limit. See $Area."
        }
        $output = $stdout.Result + [Environment]::NewLine + $stderr.Result
        [IO.File]::WriteAllText($log, $output)
        $remainingOutput = if ($ExpectedErrors) { $output -creplace $ExpectedErrors, '' } else { $output }
        if ($process.ExitCode -ne 0 -or $remainingOutput -match '(?m)^\s*(SCRIPT ERROR|ERROR):') {
            throw "Godot failed during $Phase (exit $($process.ExitCode)). See $log."
        }
        return $output
    } finally {
        if (!$process.HasExited) { $process.Kill($true) }
        $process.Dispose()
    }
}

# Ensure each authored graph ran exactly once.
function Assert-VisualScriptOutput {
    param([string]$Output, [string]$Log)

    $lines = $Output -split '\r?\n'
    foreach ($expected in @('Hello World', 'CRIMSON_VISUAL_SCRIPT_AUDIO_READY', 'CRIMSON_VISUAL_SCRIPT_AUDIO_2D_READY', 'CRIMSON_VISUAL_SCRIPT_MESH_READY', 'CRIMSON_VISUAL_SCRIPT_TESTS_PASSED')) {
        if (@($lines -ceq $expected).Count -ne 1) {
            throw "Expected exactly one '$expected'. See $Log."
        }
    }
}

# Check both the existing examples and the workbench's advanced execution fixture.
function Test-VisualScriptRuntime {
    param([string]$Project, [string]$Area, [string]$Prefix, [string[]]$RunOptions = @())

    $runtimeArgs = $RunOptions + @('--script', 'res://tests/visual_scripting/runtime.gd')
    $output = Invoke-VisualScriptGodot -Project $Project -Area $Area -Phase "$Prefix-runtime" -Arguments $runtimeArgs
    Assert-VisualScriptOutput -Output $output -Log (Join-Path $Area "$Prefix-runtime.log")
    $advancedArgs = $RunOptions + @('--script', 'res://tests/visual_scripting/advanced_runtime.gd')
    $advanced = Invoke-VisualScriptGodot -Project $Project -Area $Area -Phase "$Prefix-advanced" -Arguments $advancedArgs
    if ($advanced -notmatch '(?m)^CRIMSON_VISUAL_SCRIPT_ADVANCED_PASSED\r?$') {
        throw "Advanced graph checks did not finish. See $(Join-Path $Area "$Prefix-advanced.log")."
    }
    $exportsArgs = $RunOptions + @('--script', 'res://tests/visual_scripting/exported_variables_runtime.gd')
    $exports = Invoke-VisualScriptGodot -Project $Project -Area $Area -Phase "$Prefix-exported-variables" -Arguments $exportsArgs
    if ($exports -notmatch '(?m)^CRIMSON_VISUAL_SCRIPT_EXPORTED_VARIABLES_RUNTIME_PASSED\r?$') {
        throw "Exported variable runtime checks did not finish. See $(Join-Path $Area "$Prefix-exported-variables.log")."
    }
    Write-Host "CRIMSON_VISUAL_SCRIPT_EXPORTED_VARIABLES_RUNTIME_PASSED $Prefix"
    $lifecycleArgs = $RunOptions + @('--script', 'res://tests/visual_scripting/lifecycle_runtime.gd')
    $lifecycle = Invoke-VisualScriptGodot -Project $Project -Area $Area -Phase "$Prefix-lifecycle" -Arguments $lifecycleArgs
    if ($lifecycle -notmatch '(?m)^CRIMSON_VISUAL_SCRIPT_LIFECYCLE_PASSED\r?$') {
        throw "Lifecycle graph checks did not finish. See $(Join-Path $Area "$Prefix-lifecycle.log")."
    }
    Test-CustomScriptRuntime -Project $Project -Area $Area -Phase "$Prefix-custom" -RunOptions $RunOptions
    $exampleArgs = $RunOptions + @('res://tests/visual_scripting/workbench_example.tscn', '--quit-after', '8')
    $example = Invoke-VisualScriptGodot -Project $Project -Area $Area -Phase "$Prefix-example" -Arguments $exampleArgs
    if (@(($example -split '\r?\n') -ceq 'The training room is ready.').Count -ne 1) {
        throw "The authored workbench example did not print once. See $(Join-Path $Area "$Prefix-example.log")."
    }
}

# Compile one native call card per signature in the vendor Godot API file.
function Test-GodotSignatureCatalog {
    param([string]$Project, [string]$Area)

    $arguments = @('--script', 'res://tests/visual_scripting/catalog_runtime.gd', '--', $pinnedApi)
    $output = Invoke-VisualScriptGodot -Project $Project -Area $Area -Phase 'godot-signature-catalog' -Arguments $arguments
    if ($output -notmatch '(?m)^CRIMSON_GODOT_SIGNATURE_CATALOG_PASSED 999 built-in, 114 utility\r?$') {
        throw "Godot signature catalogue did not compile. See $(Join-Path $Area 'godot-signature-catalog.log')."
    }
    Write-Host 'CRIMSON_GODOT_SIGNATURE_CATALOG_PASSED 999 built-in, 114 utility'
}

# Exercise new node families in source projects with isolated fixture assets.
function Test-VisualScriptNodeMatrices {
    param([string]$Project, [string]$Area)

    $matrices = @(
        @{ File = 'cast_input_matrix.gd'; Pass = 'CRIMSON_VISUAL_SCRIPT_CAST_INPUT_MATRIX_PASSED' },
        @{ File = 'collection_resource_runtime.gd'; Pass = 'CRIMSON_VISUAL_SCRIPT_COLLECTION_RESOURCE_MATRIX_PASSED' },
        @{ File = 'value_nodes_runtime.gd'; Pass = 'CRIMSON_VISUAL_SCRIPT_VALUE_NODES_PASSED' },
        @{ File = 'await_runtime.gd'; Pass = 'CRIMSON_VISUAL_SCRIPT_AWAIT_PASSED' }
    )
    $fixture = Join-Path $Project 'tests/visual_scripting'
    foreach ($matrix in $matrices) {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot $matrix.File) -Destination $fixture
        $phase = [IO.Path]::GetFileNameWithoutExtension($matrix.File)
        $arguments = @('--script', "res://tests/visual_scripting/$($matrix.File)")
        $output = Invoke-VisualScriptGodot -Project $Project -Area $Area -Phase $phase -Arguments $arguments
        if ($output -notmatch ('(?m)^' + $matrix.Pass + '(?: [^\r\n]+)?\r?$')) {
            throw "The $phase fixture did not finish. See $(Join-Path $Area "$phase.log")."
        }
        Write-Host $matrix.Pass
    }

    $valueScript = 'res://tests/visual_scripting/value_nodes_runtime.gd'
    $valueFailures = @{
        'invalid-type' = 'Choose a supported Godot type for Make or Break\.'
        'wrong-field' = "Input 'x' is unavailable or incompatible\. Connect a value or assign its default\."
    }
    foreach ($mode in $valueFailures.Keys) {
        $expected = $valueFailures[$mode]
        $arguments = @('--script', $valueScript, '--', "--$mode")
        $output = Invoke-VisualScriptGodot -Project $Project -Area $Area -Phase "value-$mode" -Arguments $arguments `
            -ExpectedErrors ('(?m)^ERROR: \[Crimson\][^\r\n]*' + $expected + '[^\r\n]*\r?$')
        if ($output -notmatch $expected -or $output -notmatch '(?m)^CRIMSON_VISUAL_SCRIPT_VALUE_NODES_REJECTION_PASSED\r?$') {
            throw "The value-$mode fixture did not reject invalid input. See $(Join-Path $Area "value-$mode.log")."
        }
    }

    $preloadScript = 'res://tests/visual_scripting/collection_resource_runtime.gd'
    $dictionaryTypeArgs = @('--script', $preloadScript, '--', '--constructor-type-error')
    $dictionaryTypeError = 'A wire has incompatible or occupied pins\.'
    $dictionaryTypeResult = Invoke-VisualScriptGodot -Project $Project -Area $Area -Phase 'dictionary-constructor-type-error' -Arguments $dictionaryTypeArgs -ExpectedErrors ('(?m)^ERROR: \[Crimson\][^\r\n]*' + $dictionaryTypeError + '[^\r\n]*\r?$')
    if ($dictionaryTypeResult -notmatch $dictionaryTypeError -or
            $dictionaryTypeResult -notmatch '(?m)^CRIMSON_VISUAL_SCRIPT_CONSTRUCTOR_TYPE_REJECTION_PASSED\r?$') {
        throw "Make Dictionary did not reject a mismatched typed input. See $(Join-Path $Area 'dictionary-constructor-type-error.log')."
    }
    $preloadArgs = @('--script', $preloadScript, '--', '--preload-errors')
    $preloadErrors = '(?m)^ERROR: \[Crimson\] Could not prepare Visual Script ''[^\r\n]*''\. Node 3\. Preload Resource could not load a [^\r\n]*\r?$'
    $preloadResult = Invoke-VisualScriptGodot -Project $Project -Area $Area -Phase 'preload-errors' -Arguments $preloadArgs `
        -ExpectedErrors $preloadErrors
    if ($preloadResult -notmatch $preloadErrors -or $preloadResult -notmatch '(?m)^CRIMSON_VISUAL_SCRIPT_PRELOAD_FAILURE_MATRIX_PASSED\r?$') {
        throw "The stale and incompatible preload checks did not finish. See $(Join-Path $Area 'preload-errors.log')."
    }

    $waitFailures = @{
        'negative-delay' = 'Delay Seconds must be finite and nonnegative'
        'nonfinite-delay' = 'Delay Seconds must be finite and nonnegative'
        'loop-body' = 'Delay and Await Signal need an Event Graph path outside a loop or a synchronous call'
        'missing-signal' = 'Connect a live object and an available signal before waiting'
    }
    foreach ($mode in $waitFailures.Keys) {
        $expected = '(?m)^ERROR: \[Crimson\][^\r\n]*' + $waitFailures[$mode] + '[^\r\n]*\r?$'
        $arguments = @('--script', 'res://tests/visual_scripting/await_runtime.gd', '--', $mode)
        $output = Invoke-VisualScriptGodot -Project $Project -Area $Area -Phase "wait-$mode" -Arguments $arguments -ExpectedErrors $expected
        if ($output -notmatch $expected -or $output -notmatch "(?m)^CRIMSON_VISUAL_SCRIPT_AWAIT_REJECTION_PASSED $mode\r?$") {
            throw "The wait-$mode fixture did not reject invalid input. See $(Join-Path $Area "wait-$mode.log")."
        }
    }
}

# Execute a Custom Script node from source, a selected-resource pack, or the saved editor fixture.
function Test-CustomScriptRuntime {
    param([string]$Project, [string]$Area, [string]$Phase, [string[]]$RunOptions = @(), [switch]$EditorAuthored)

    $arguments = $RunOptions + @('--script', 'res://tests/visual_scripting/custom_script_runtime.gd')
    if ($EditorAuthored) { $arguments += @('--', '--editor-authored') }
    $output = Invoke-VisualScriptGodot -Project $Project -Area $Area -Phase $Phase -Arguments $arguments
    if ($output -notmatch '(?m)^CRIMSON_VISUAL_SCRIPT_CUSTOM_RUNTIME_PASSED\r?$') {
        throw "Custom Script execution checks did not finish. See $(Join-Path $Area "$Phase.log")."
    }
    Write-Host "CRIMSON_VISUAL_SCRIPT_CUSTOM_RUNTIME_PASSED $Phase"
}

# A synchronous node must block Then when the body is rejected or Godot reports a VM failure.
function Test-CustomScriptFailure {
    param(
        [string]$Project,
        [string]$Area,
        [ValidateSet('custom-failure', 'custom-suspension', 'custom-early-return', 'custom-static-init', 'custom-source-initializer')][string]$Phase
    )

    if ($Phase -eq 'custom-suspension') {
        $diagnostic = 'Custom Script runs synchronously and cannot await\.'
        $expectedErrors = '(?m)^ERROR: \[Crimson\] Could not prepare Visual Script .+Custom Script runs synchronously and cannot await\.\r?$'
    } elseif ($Phase -eq 'custom-static-init') {
        $diagnostic = "'_static_init' is reserved\."
        $expectedErrors = '(?m)^ERROR: \[Crimson\] Could not prepare Visual Script .+_static_init.+ is reserved\. Choose another helper function name\.\r?$'
    } elseif ($Phase -eq 'custom-source-initializer') {
        $diagnostic = '(?m)^SCRIPT ERROR: Parse Error: [^\r\n]*"String"[^\r\n]*"int"[^\r\n]*\r?$'
        $expectedErrors = $diagnostic + '|^ERROR: \[Crimson\] Could not prepare Visual Script .+Custom Script could not compile\.[^\r\n]*\r?$'
    } else {
        $diagnostic = 'Custom Script did not finish\.'
        $expectedErrors = '(?m)^SCRIPT ERROR: [^\r\n]*nonexistent_call[^\r\n]*\r?$|^ERROR: \[Crimson\] .+Custom Script did not finish\. Check its GDScript errors\.\r?$'
    }
    $arguments = @('--script', 'res://tests/visual_scripting/custom_script_runtime.gd', '--', "--$phase")
    $output = Invoke-VisualScriptGodot -Project $Project -Area $Area -Phase $phase -Arguments $arguments -ExpectedErrors $expectedErrors
    if ($output -notmatch '(?m)^CRIMSON_VISUAL_SCRIPT_CUSTOM_FAILURE_PASSED\r?$' -or $output -notmatch $diagnostic -or
            $output -match '(?m)^CRIMSON_UNEXPECTED_STATIC_INIT\r?$') {
        throw "Custom Script failure propagation did not finish. See $(Join-Path $Area "$phase.log")."
    }
    Write-Host "CRIMSON_VISUAL_SCRIPT_CUSTOM_FAILURE_PASSED $phase"
}

# Attach a loopback debugger to a separate game and verify break, inspect, step, and continue.
function Test-VisualScriptDebugger {
    param([string]$Project, [string]$Area)

    $fixture = Join-Path $Project 'tests/visual_scripting'
    foreach ($entry in @('debugger_smoke.gd', 'debugger_signal.crimsonscript', 'debugger_signal.tscn',
            'debugger_wait.crimsonscript', 'debugger_wait.tscn')) {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot $entry) -Destination $fixture
    }
    $arguments = @('--script', 'res://tests/visual_scripting/debugger_smoke.gd')
    $output = Invoke-VisualScriptGodot -Project $Project -Area $Area -Phase 'debugger' -Arguments $arguments
    if ($output -notmatch '(?m)^CRIMSON_VISUAL_SCRIPT_DEBUGGER_PASSED\r?$') {
        throw "Graph debugger checks did not finish. See $(Join-Path $Area 'debugger.log')."
    }
    Write-Host 'CRIMSON_VISUAL_SCRIPT_DEBUGGER_PASSED'
}

# Start a disposable editor with an isolated test plugin that drives native controls.
function Test-VisualScriptEditor {
    param([string]$Project, [string]$Area)

    $plugin = Join-Path $Project 'addons/visual_script_test'
    New-Item -ItemType Directory -Path $plugin -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'editor_smoke.gd') -Destination $plugin
    @'
; Copyright Francesco Di Blasi 2026 - All Rights Reserved

[plugin]

name="Visual Script Editor Fixture"
script="editor_smoke.gd"
'@ | Set-Content -Encoding utf8NoBOM -LiteralPath (Join-Path $plugin 'plugin.cfg')
    @'

[editor_plugins]
enabled=PackedStringArray("res://addons/visual_script_test/plugin.cfg")
'@ | Add-Content -Encoding utf8NoBOM -LiteralPath (Join-Path $Project 'project.godot')

    $editorArgs = @('--editor', '--dap-port', '36148', '--lsp-port', '36147')
    $output = Invoke-VisualScriptGodot -Project $Project -Area $Area -Phase 'editor' -Arguments $editorArgs
    if ($output -notmatch '(?m)^CRIMSON_VISUAL_SCRIPT_EDITOR_PASSED\r?$') {
        throw "Editor graph checks did not finish. See $(Join-Path $Area 'editor.log')."
    }
    Write-Host 'CRIMSON_VISUAL_SCRIPT_EDITOR_PASSED'

    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'editor_authoring.gd') -Destination $plugin
    $pluginConfig = Join-Path $plugin 'plugin.cfg'
    ((Get-Content -Raw -LiteralPath $pluginConfig).Replace('editor_smoke.gd', 'editor_authoring.gd')) |
        Set-Content -Encoding utf8NoBOM -LiteralPath $pluginConfig
    $authoringArgs = @('--editor', '--dap-port', '36258', '--lsp-port', '36257')
    $authoringOutput = Invoke-VisualScriptGodot -Project $Project -Area $Area -Phase 'editor-authoring' -Arguments $authoringArgs
    if ($authoringOutput -notmatch '(?m)^CRIMSON_AUTHORED_ENUM_COLLECTION_FUNCTION_PASSED\r?$') {
        throw "Editor authoring checks did not finish. See $(Join-Path $Area 'editor-authoring.log')."
    }
    Write-Host 'CRIMSON_AUTHORED_ENUM_COLLECTION_FUNCTION_PASSED'

    $matrixArgs = @('--editor', '--dap-port', '36658', '--lsp-port', '36657', '--', '--collection-interaction-matrix-only')
    $matrixOutput = Invoke-VisualScriptGodot -Project $Project -Area $Area -Phase 'editor-collection-matrix' -Arguments $matrixArgs
    if ($matrixOutput -notmatch '(?m)^CRIMSON_COLLECTION_INTERACTION_MATRIX_PASSED 28\r?$') {
        throw "Collection interaction matrix did not finish. See $(Join-Path $Area 'editor-collection-matrix.log')."
    }
    Write-Host 'CRIMSON_COLLECTION_INTERACTION_MATRIX_PASSED 28'

    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'exported_variables_editor.gd') -Destination $plugin
    ((Get-Content -Raw -LiteralPath $pluginConfig).Replace('editor_authoring.gd', 'exported_variables_editor.gd')) |
        Set-Content -Encoding utf8NoBOM -LiteralPath $pluginConfig
    $exportsArgs = @('--editor', '--dap-port', '36668', '--lsp-port', '36667')
    $exports = Invoke-VisualScriptGodot -Project $Project -Area $Area -Phase 'editor-exported-variables' -Arguments $exportsArgs
    if ($exports -notmatch '(?m)^CRIMSON_VISUAL_SCRIPT_EXPORTED_VARIABLES_EDITOR_PASSED\r?$') {
        throw "Exported variable Inspector checks did not finish. See $(Join-Path $Area 'editor-exported-variables.log')."
    }
    Write-Host 'CRIMSON_VISUAL_SCRIPT_EXPORTED_VARIABLES_EDITOR_PASSED'

    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'editor_debugger.gd') -Destination $plugin
    ((Get-Content -Raw -LiteralPath $pluginConfig).Replace('exported_variables_editor.gd', 'editor_debugger.gd')) |
        Set-Content -Encoding utf8NoBOM -LiteralPath $pluginConfig
    $debuggerArgs = @('--editor', '--dap-port', '36358', '--lsp-port', '36357', '--debug-server', 'tcp://127.0.0.1:36359')
    $debuggerOutput = Invoke-VisualScriptGodot -Project $Project -Area $Area -Phase 'editor-debugger' -Arguments $debuggerArgs
    if ($debuggerOutput -notmatch '(?m)^CRIMSON_VISUAL_SCRIPT_EMBEDDED_DEBUGGER_PASSED\r?$') {
        throw "Embedded graph debugger checks did not finish. See $(Join-Path $Area 'editor-debugger.log')."
    }
    Write-Host 'CRIMSON_VISUAL_SCRIPT_EMBEDDED_DEBUGGER_PASSED'

    Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../input/editor_ui_smoke.gd') -Destination $plugin
    ((Get-Content -Raw -LiteralPath $pluginConfig).Replace('editor_debugger.gd', 'editor_ui_smoke.gd')) |
        Set-Content -Encoding utf8NoBOM -LiteralPath $pluginConfig
    $sharedUiArgs = @('--editor', '--dap-port', '36458', '--lsp-port', '36457')
    $sharedUiOutput = Invoke-VisualScriptGodot -Project $Project -Area $Area -Phase 'editor-shared-ui' -Arguments $sharedUiArgs
    if ($sharedUiOutput -notmatch '(?m)^CRIMSON_SHARED_EDITOR_UI_PASSED\r?$') {
        throw "Shared editor UI checks did not finish. See $(Join-Path $Area 'editor-shared-ui.log')."
    }
    Write-Host 'CRIMSON_SHARED_EDITOR_UI_PASSED'

    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'editor_custom_script.gd') -Destination $plugin
    ((Get-Content -Raw -LiteralPath $pluginConfig).Replace('editor_ui_smoke.gd', 'editor_custom_script.gd')) |
        Set-Content -Encoding utf8NoBOM -LiteralPath $pluginConfig
    $customArgs = @('--editor', '--dap-port', '36558', '--lsp-port', '36557', '--', '--crimson-lsp-port=36557')
    $customOutput = Invoke-VisualScriptGodot -Project $Project -Area $Area -Phase 'editor-custom' -Arguments $customArgs
    if ($customOutput -notmatch '(?m)^CRIMSON_VISUAL_SCRIPT_CUSTOM_EDITOR_PASSED\r?$') {
        throw "Custom Script CodeEdit checks did not finish. See $(Join-Path $Area 'editor-custom.log')."
    }
    Write-Host 'CRIMSON_VISUAL_SCRIPT_CUSTOM_EDITOR_PASSED'
    Test-CustomScriptRuntime -Project $Project -Area $Area -Phase 'editor-authored-custom' -EditorAuthored

    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'editor_attach_script.gd') -Destination $plugin
    ((Get-Content -Raw -LiteralPath $pluginConfig).Replace('editor_custom_script.gd', 'editor_attach_script.gd')) |
        Set-Content -Encoding utf8NoBOM -LiteralPath $pluginConfig
    $attachArgs = @('--editor', '--dap-port', '36678', '--lsp-port', '36677')
    $attachment = Invoke-VisualScriptGodot -Project $Project -Area $Area -Phase 'editor-attach-script' -Arguments $attachArgs
    if ($attachment -notmatch '(?m)^CRIMSON_VISUAL_SCRIPT_EDITOR_ATTACH_PASSED\r?$') {
        throw "Scene dock graph attachment checks did not finish. See $(Join-Path $Area 'editor-attach-script.log')."
    }
    Write-Host 'CRIMSON_VISUAL_SCRIPT_EDITOR_ATTACH_PASSED'

    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'editor_node_actions.gd') -Destination $plugin
    ((Get-Content -Raw -LiteralPath $pluginConfig).Replace('editor_attach_script.gd', 'editor_node_actions.gd')) |
        Set-Content -Encoding utf8NoBOM -LiteralPath $pluginConfig
    $matrixArgs = @('--editor', '--dap-port', '36708', '--lsp-port', '36707')
    $nodeActions = Invoke-VisualScriptGodot -Project $Project -Area $Area -Phase 'editor-node-actions' -Arguments $matrixArgs
    if ($nodeActions -notmatch '(?m)^CRIMSON_VISUAL_SCRIPT_EDITOR_NODE_ACTIONS_PASSED(?: [^\r\n]+)?\r?$') {
        throw "Editor node action checks did not finish. See $(Join-Path $Area 'editor-node-actions.log')."
    }
    Write-Host 'CRIMSON_VISUAL_SCRIPT_EDITOR_NODE_ACTIONS_PASSED'

    $editorResourceCases = @(
        @{ Scene = 'editor_node_actions_load.tscn'; Phase = 'editor-authored-load'; Message = 'CRIMSON_EDITOR_AUTHORED_RESOURCE_LOADED' },
        @{ Scene = 'editor_node_actions_constructor.tscn'; Phase = 'editor-authored-constructor'; Message = 'CRIMSON_EDITOR_AUTHORED_SCRIPT_CONSTRUCTED 73' }
    )
    foreach ($resourceCase in $editorResourceCases) {
        $resourceArgs = @("res://$($resourceCase.Scene)", '--quit-after', '3')
        $resourceOutput = Invoke-VisualScriptGodot -Project $Project -Area $Area -Phase $resourceCase.Phase -Arguments $resourceArgs
        if (@(($resourceOutput -split '\r?\n') -ceq $resourceCase.Message).Count -ne 1) {
            throw "Expected one '$($resourceCase.Message)'. See $(Join-Path $Area ($resourceCase.Phase + '.log'))."
        }
        Write-Host $resourceCase.Message
    }

}

# Run tag editor fixtures with their own catalog, scene, editor layout, and application data.
function Test-GameplayTagsEditor {
    param([string]$Project, [string]$Area)

    $cases = @(
        @{ File = 'editor_ui.gd'; Phase = 'gameplay-tags-inspector'; Pass = 'CRIMSON_GAMEPLAY_TAG_EDITOR_UI_PASSED'; Dap = '36718'; Lsp = '36717' },
        @{ File = 'visual_script_editor.gd'; Phase = 'gameplay-tags-graph'; Pass = 'CRIMSON_VISUAL_SCRIPT_GAMEPLAY_TAG_PICKER_PASSED'; Dap = '36728'; Lsp = '36727' }
    )
    foreach ($case in $cases) {
        $isolatedArea = Join-Path $Area $case.Phase
        $isolatedProject = Join-Path $isolatedArea 'project'
        $addons = Join-Path $isolatedProject 'addons'
        $plugin = Join-Path $addons 'gameplay_tags_test'
        $metadata = Join-Path $isolatedProject '.godot'
        New-Item -ItemType Directory -Path $plugin, $metadata -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $Project 'addons/crimson-framework') -Destination $addons -Recurse
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot "../gameplay_tags/$($case.File)") -Destination $plugin
        @"
; Copyright Francesco Di Blasi 2026 - All Rights Reserved

[plugin]

name="Crimson Gameplay Tags Fixture"
script="$($case.File)"
"@ | Set-Content -Encoding utf8NoBOM -LiteralPath (Join-Path $plugin 'plugin.cfg')
        @'
config_version=5

[application]
config/name="Crimson Gameplay Tags Test"

[editor_plugins]
enabled=PackedStringArray("res://addons/gameplay_tags_test/plugin.cfg")

[rendering]
renderer/rendering_method="gl_compatibility"
'@ | Set-Content -Encoding utf8NoBOM -LiteralPath (Join-Path $isolatedProject 'project.godot')
        Set-Content -Encoding utf8NoBOM -LiteralPath (Join-Path $metadata 'extension_list.cfg') -Value 'res://addons/crimson-framework/crimson-framework.gdextension'
        $arguments = @('--editor', '--dap-port', $case.Dap, '--lsp-port', $case.Lsp, '--quit-after', '2400')
        $output = Invoke-VisualScriptGodot -Project $isolatedProject -Area $isolatedArea -Phase $case.Phase -Arguments $arguments
        if ($output -notmatch ('(?m)^' + $case.Pass + '\r?$')) {
            throw "Gameplay Tags editor checks did not finish. See $isolatedArea."
        }
        Write-Host $case.Pass
    }
}

# Test the docked Visual Script screen and native pointer/focus in an isolated project.
function Test-VisualScriptNativeFocus {
    param([string]$Project, [string]$Area)

    $nativeProject = Join-Path $Area 'native-project'
    $addonDirectory = Join-Path $nativeProject 'addons'
    New-Item -ItemType Directory -Path $addonDirectory -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $Project 'addons/crimson-framework') -Destination $addonDirectory -Recurse
    @'
config_version=5

[application]
config/name="Crimson Visual Script Native Test"

[editor_plugins]
enabled=PackedStringArray("res://addons/visual_script_test/plugin.cfg")

[rendering]
renderer/rendering_method="gl_compatibility"
'@ | Set-Content -Encoding utf8NoBOM -LiteralPath (Join-Path $nativeProject 'project.godot')

    $plugin = Join-Path $nativeProject 'addons/visual_script_test'
    New-Item -ItemType Directory -Path $plugin -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'editor_native_focus.gd') -Destination $plugin
    @'
; Copyright Francesco Di Blasi 2026 - All Rights Reserved

[plugin]

name="Visual Script Native Focus Fixture"
script="editor_native_focus.gd"
'@ | Set-Content -Encoding utf8NoBOM -LiteralPath (Join-Path $plugin 'plugin.cfg')
    $arguments = @('--editor', '--rendering-method', 'gl_compatibility', '--resolution', '520x350',
        '--max-fps', '30', '--quit-after', '240', '--dap-port', '37688', '--lsp-port', '37687')
    $output = Invoke-VisualScriptGodot -Project $nativeProject -Area $Area -Phase 'editor-native-focus' -Arguments $arguments -NativeWindow
    if ($output -notmatch '(?m)^CRIMSON_NATIVE_FOCUS_GRAPH_PASSED ') {
        throw "Native graph focus checks did not finish. See $(Join-Path $Area 'editor-native-focus.log')."
    }
    Write-Host 'CRIMSON_NATIVE_FOCUS_GRAPH_PASSED'
}

# Select the entry graph files so their preloaded resources are discovered as dependencies.
function Test-VisualScriptPack {
    param([string]$Project, [string]$Area, [string]$Binary)

    $refreshArgs = @('--editor', '--import')
    Invoke-VisualScriptGodot -Project $Project -Area $Area -Phase 'import-generated-resources' -Arguments $refreshArgs | Out-Null
    @'
[preset.0]

name="Visual Script Fixture"
platform="Windows Desktop"
runnable=true
dedicated_server=false
export_filter="resources"
export_files=PackedStringArray("res://tests/visual_scripting/ready_print.tscn", "res://tests/visual_scripting/audio_members.tscn", "res://tests/visual_scripting/workbench_example.tscn", "res://tests/visual_scripting/custom_script.tscn", "res://tests/visual_scripting/collection_export.crimsonscript", "res://tests/visual_scripting/preload_resource_fixture.crimsonscript", "res://tests/visual_scripting/collection_resource_runtime.gd", "res://resource_matrix_generated/references.crimsonscript", "res://tests/visual_scripting/runtime.gd", "res://tests/visual_scripting/advanced_runtime.gd", "res://tests/visual_scripting/foreign_controller.gd", "res://tests/visual_scripting/lifecycle_runtime.gd", "res://tests/visual_scripting/custom_script_runtime.gd", "res://tests/visual_scripting/exported_variables_runtime.gd", "res://addons/crimson-framework/crimson-framework.gdextension")
include_filter=""
exclude_filter=""

[preset.0.options]

binary_format/architecture="x86_64"
'@ | Set-Content -Encoding utf8NoBOM -LiteralPath (Join-Path $Project 'export_presets.cfg')

    $pack = Join-Path $Area 'fixture.pck'
    $packArgs = @('--export-pack', 'Visual Script Fixture', $pack)
    Invoke-VisualScriptGodot -Project $Project -Area $Area -Phase 'export-pack' -Arguments $packArgs | Out-Null
    $packedProject = Join-Path $Area 'packed-project'
    $addonBin = 'addons/crimson-framework/bin'
    $packedBin = Join-Path $packedProject $addonBin
    New-Item -ItemType Directory -Path $packedBin -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $Project "$addonBin/$Binary") -Destination $packedBin
    Test-VisualScriptRuntime -Project $packedProject -Area $Area -Prefix 'packed' -RunOptions @('--main-pack', $pack)
    $resourceArgs = @('--main-pack', $pack, '--script', 'res://tests/visual_scripting/collection_resource_runtime.gd', '--', '--packed')
    $resourceResult = Invoke-VisualScriptGodot -Project $packedProject -Area $Area -Phase 'packed-preload' -Arguments $resourceArgs
    if ($resourceResult -notmatch '(?m)^CRIMSON_VISUAL_SCRIPT_COLLECTION_RESOURCE_MATRIX_PASSED(?: [^\r\n]+)?\r?$') {
        throw "Packed resource loading did not finish. See $(Join-Path $Area 'packed-preload.log')."
    }
    Write-Host 'CRIMSON_VISUAL_SCRIPT_PACK_PASSED'
}

foreach ($variant in $Target) {
    $area = Join-Path ([IO.Path]::GetTempPath()) ("crimson-visual-script-$variant-" + [guid]::NewGuid().ToString('N'))
    $project = Join-Path $area 'project'
    $addon = Join-Path $project 'addons/crimson-framework'
    New-Item -ItemType Directory -Path (Join-Path $addon 'bin') -Force | Out-Null
    foreach ($entry in @('images', 'input', 'editor')) {
        Copy-Item -LiteralPath (Join-Path $sourceAddon $entry) -Destination $addon -Recurse
    }
    $binary = "crimson_framework.windows.$variant.x86_64.dll"
    Copy-Item -LiteralPath (Join-Path $binarySource $binary) -Destination (Join-Path $addon 'bin')
    $variantDescriptor = $descriptor
    if ($variant -eq 'template_release') {
        $variantDescriptor = $variantDescriptor.Replace('.template_debug.', '.template_release.')
    }
    Set-Content -Encoding utf8NoBOM -LiteralPath (Join-Path $addon 'crimson-framework.gdextension') -Value $variantDescriptor
    $fixture = Join-Path $project 'tests/visual_scripting'
    New-Item -ItemType Directory -Path $fixture -Force | Out-Null
    $fixtureFiles = @('ready_print.crimsonscript', 'ready_print.tscn', 'audio_members.crimsonscript', 'audio_members.tscn',
        'workbench_example.crimsonscript', 'workbench_example.tscn', 'ready_chime.wav', 'ready_chime.wav.import', 'runtime.gd', 'advanced_runtime.gd', 'lifecycle_runtime.gd',
        'debugger_steps.crimsonscript', 'debugger_steps.tscn', 'custom_script.crimsonscript', 'custom_script.tscn', 'custom_script_alarm.tres',
        'custom_script_runtime.gd', 'collection_export.crimsonscript', 'collection_asset_only.tscn', 'foreign_controller.gd',
        'catalog_runtime.gd', 'exported_variables_runtime.gd', 'await_emitter.gd',
        'preload_resource_fixture.crimsonscript', 'preload_resource_asset.tres')
    foreach ($entry in $fixtureFiles) {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot $entry) -Destination $fixture
    }
    $tagsFixture = Join-Path $project 'tests/gameplay_tags'
    New-Item -ItemType Directory -Path $tagsFixture -Force | Out-Null
    foreach ($entry in @('inspector_preview.gd', 'inspector_preview.tscn', 'runtime.gd')) {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot "../gameplay_tags/$entry") -Destination $tagsFixture
    }
    @'
config_version=5

[application]
config/name="Crimson Visual Script Test"

[editor]
run/main_run_args="--headless --max-fps 60 --quit-after 75 --log-file user://embedded-visual-debugger.log"

[autoload]
CrimsonFixtureAutoload="*res://tests/visual_scripting/fixture_autoload.gd"
CrimsonFixtureSceneAutoload="*res://tests/visual_scripting/fixture_scene_autoload.tscn"

[rendering]
renderer/rendering_method="gl_compatibility"
'@ | Set-Content -Encoding utf8NoBOM -LiteralPath (Join-Path $project 'project.godot')
    @'
# Copyright Francesco Di Blasi 2026 - All Rights Reserved
extends Node3D

func fixture_score() -> int:
    return 25
'@ | Set-Content -Encoding utf8NoBOM -LiteralPath (Join-Path $fixture 'fixture_autoload.gd')
    @'
[gd_scene load_steps=2 format=3]

[ext_resource type="Script" path="res://tests/visual_scripting/fixture_autoload.gd" id="1"]

[node name="FixtureSceneAutoload" type="Node3D"]
script = ExtResource("1")
'@ | Set-Content -Encoding utf8NoBOM -LiteralPath (Join-Path $fixture 'fixture_scene_autoload.tscn')

    Write-Host "Testing Visual Script with $variant. Logs and isolated data: $area"
    $importArgs = @('--editor', '--import', '--frame-delay', '1000')
    Invoke-VisualScriptGodot -Project $project -Area $area -Phase 'import' -Arguments $importArgs | Out-Null
    Test-VisualScriptRuntime -Project $project -Area $area -Prefix 'source'
    $tagsArgs = @('--script', 'res://tests/gameplay_tags/runtime.gd')
    $tagsResult = Invoke-VisualScriptGodot -Project $project -Area $area -Phase 'gameplay-tags' -Arguments $tagsArgs
    if ($tagsResult -notmatch '(?m)^CRIMSON_GAMEPLAY_TAGS_RUNTIME_PASSED\r?$') {
        throw "Gameplay Tags runtime and typed graph checks did not finish. See $area."
    }
    Write-Host "CRIMSON_GAMEPLAY_TAGS_RUNTIME_PASSED $variant"
    Test-VisualScriptNodeMatrices -Project $project -Area $area
    Test-GodotSignatureCatalog -Project $project -Area $area
    Test-CustomScriptFailure -Project $project -Area $area -Phase 'custom-suspension'
    Test-CustomScriptFailure -Project $project -Area $area -Phase 'custom-early-return'
    Test-CustomScriptFailure -Project $project -Area $area -Phase 'custom-static-init'
    Test-CustomScriptFailure -Project $project -Area $area -Phase 'custom-source-initializer'
    if ($variant -eq 'template_debug') {
        Test-CustomScriptFailure -Project $project -Area $area -Phase 'custom-failure'
    }
    Write-Host "CRIMSON_VISUAL_SCRIPT_TESTS_PASSED $variant"
    if ($VerifyPack -and $variant -eq 'template_release') {
        Test-VisualScriptPack -Project $project -Area $area -Binary $binary
    }
    if (($VerifyEditor -or $VerifyDebugger) -and $variant -eq 'template_debug') {
        Test-VisualScriptDebugger -Project $project -Area $area
    }
    if ($VerifyEditor -and $variant -eq 'template_debug') {
        Test-VisualScriptEditor -Project $project -Area $area
        Test-GameplayTagsEditor -Project $project -Area $area
    }
    if ($VerifyNativeFocus -and $variant -eq 'template_debug') {
        Test-VisualScriptNativeFocus -Project $project -Area $area
    }
}
