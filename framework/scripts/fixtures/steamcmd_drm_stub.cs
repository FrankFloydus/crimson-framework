// Copyright Francesco Di Blasi 2026 - All Rights Reserved

using System;
using System.IO;

// Records a local SteamCMD invocation and simulates its output and exit status.
internal static class SteamCmdDrmStub
{
    // Distinguish the test input and the staged output without a real game executable.
    private const string InputContent = "crimson-export-fixture";
    private const string OutputContent = "crimson-wrapped-fixture";
    private const string CompetingContent = "crimson-competing-output";

    // Validate the full argument vector before producing any staged file.
    private static int Main(string[] args)
    {
        string root = Environment.GetEnvironmentVariable("CRIMSON_STEAM_DRM_FIXTURE_ROOT");
        string scenario = Environment.GetEnvironmentVariable("CRIMSON_STEAM_DRM_FIXTURE_CASE");
        if (String.IsNullOrEmpty(root) || String.IsNullOrEmpty(scenario))
        {
            return 90;
        }

        File.AppendAllText(Path.Combine(root, "native-invocations.txt"), scenario + Environment.NewLine);
        string depot = Path.Combine(root, "depot build");
        string source = Path.Combine(root, "game export", "demo export.exe");
        string expectedFlag = scenario == "compatibility" ? "6" : "0";
        if (args.Length != 9 || args[0] != "+login" || args[1] != "crimson_fixture" ||
            args[2] != "+drm_wrap" || args[3] != "1234567" || args[4] != source ||
            args[6] != "drmtoolp" || args[7] != expectedFlag || args[8] != "+quit" ||
            Directory.GetCurrentDirectory() != Path.Combine(root, "steam tool") ||
            File.ReadAllText(source) != InputContent)
        {
            Console.Error.WriteLine("Unexpected SteamCMD DRM arguments or working directory.");
            return 91;
        }

        string stage = Path.GetDirectoryName(args[5]);
        if (Path.GetDirectoryName(stage) != depot || !Path.GetFileName(stage).StartsWith(".steam-wrap-") ||
            Path.GetFileName(args[5]) != "wrapped.exe")
        {
            Console.Error.WriteLine("Wrapped output must be staged under the destination directory.");
            return 92;
        }

        switch (scenario)
        {
            case "normal":
            case "compatibility":
                File.WriteAllText(args[5], OutputContent);
                return 0;
            case "native-error":
                File.WriteAllText(args[5], OutputContent);
                return 17;
            case "missing-output":
                return 0;
            case "empty-output":
                File.WriteAllText(args[5], String.Empty);
                return 0;
            case "raced-output":
                File.WriteAllText(args[5], OutputContent);
                File.WriteAllText(Path.Combine(depot, "raced-output.exe"), CompetingContent);
                return 0;
            default:
                return 93;
        }
    }
}
