// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using Fm4StaticRecomp.Launcher.Core.Install;
using Fm4StaticRecomp.Launcher.Core.Setup;

namespace Fm4StaticRecomp.Launcher.Core.Updates;

public sealed class VersionBuilder(InstallLayout layout, VersionStore versions, SetupState state, IBuildRunner runner)
{
    // Builds a version in its own source folder from a fresh build folder (spec 13: a copied
    // build tree cannot build incrementally); the active version stays as it is until this
    // one has built.
    public async Task<SetupFailure?> BuildVersionAsync(SetupRequest request, string payloadZip, IProgress<SetupEvent> progress,
        Action<string> logLine, CancellationToken cancellation)
    {
        await versions.ExtractPayloadAsync(payloadZip, request.Version, cancellation);
        if (!File.Exists(layout.SetupScript(request.Version)))
            return new SetupFailure($"The source of version {request.Version} has no {Path.GetFileName(layout.SetupScript(request.Version))}.", "tools", 1, [],
                "Download the release zip again.", null, layout.SetupLogs(request.Version));
        var failure = await runner.BuildAsync(request, progress, logLine, cancellation);
        if (failure is not null) return failure;
        state.BuiltVersions.Add(request.Version);
        state.Save();
        versions.Activate(request.Version);
        foreach (var folder in await Task.Run(versions.Prune, CancellationToken.None))
            logLine($"{folder} could not be deleted; it is removed after the next update.");
        return null;
    }
}
