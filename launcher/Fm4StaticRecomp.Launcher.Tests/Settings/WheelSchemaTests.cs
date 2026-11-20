// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using Fm4StaticRecomp.Launcher.Core.Settings;

namespace Fm4StaticRecomp.Launcher.Tests.Settings;

public class WheelSchemaTests
{
    public static WheelSchema Sample() =>
        WheelSchema.Load(Path.Combine(AppContext.BaseDirectory, "Settings", "wheel-schema-sample.json"));

    [Fact]
    public void Loads_the_schema_the_game_writes()
    {
        var schema = Sample();
        Assert.Equal(9, schema.Rows.Count);
        Assert.Equal(3, schema.Types.Count);
        Assert.Equal(["direct", "belt", "gear"], schema.Types.Select(t => t.Id));
        Assert.Equal("STRENGTH", schema.Rows[0].Label);
        Assert.True(schema.Rows[0].Basic);
        var damper = schema.Rows.Single(r => r.Key == "wheel_ffb_damper");
        Assert.Equal((0, 200, 10, 100), (damper.Min, damper.Max, damper.Step, damper.Fallback));
        Assert.Equal(80, schema.Types.Single(t => t.Id == "belt").Values["wheel_ffb_damper"]);
        Assert.Equal("difference", schema.Expert.Single(e => e.Key == "wheel_ffb_mode").Default);
        Assert.DoesNotContain(schema.Expert, e => e.Key == "wheel_steering_scale");
    }

    [Fact]
    public void Refuses_an_unknown_version()
    {
        using var temp = new TempDir();
        var path = temp.File("w.json", """{"version":2,"rows":[],"types":[],"expert":[]}"""u8.ToArray());
        Assert.Throws<InvalidDataException>(() => WheelSchema.Load(path));
    }
}
