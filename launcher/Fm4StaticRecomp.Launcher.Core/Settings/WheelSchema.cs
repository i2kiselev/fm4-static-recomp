// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Fm4StaticRecomp.Launcher.Core.Settings;

public sealed record WheelRow(
    [property: JsonPropertyName("key")] string Key,
    [property: JsonPropertyName("label")] string Label,
    [property: JsonPropertyName("description")] string Description,
    [property: JsonPropertyName("min")] int Min,
    [property: JsonPropertyName("max")] int Max,
    [property: JsonPropertyName("step")] int Step,
    [property: JsonPropertyName("fallback")] int Fallback,
    [property: JsonPropertyName("basic")] bool Basic);

public sealed record WheelPreset(
    [property: JsonPropertyName("id")] string Id,
    [property: JsonPropertyName("label")] string Label,
    [property: JsonPropertyName("values")] IReadOnlyDictionary<string, int> Values);

public sealed record WheelExpertSetting(
    [property: JsonPropertyName("key")] string Key,
    [property: JsonPropertyName("description")] string Description,
    [property: JsonPropertyName("default")] string Default);

// What the game exe writes with --fm4_write_wheel_schema (src\wheel_settings.cpp WriteWheelSchema).
public sealed record WheelSchema(
    [property: JsonPropertyName("version")] int Version,
    [property: JsonPropertyName("rows")] IReadOnlyList<WheelRow> Rows,
    [property: JsonPropertyName("types")] IReadOnlyList<WheelPreset> Types,
    [property: JsonPropertyName("expert")] IReadOnlyList<WheelExpertSetting> Expert)
{
    public const string SteeringLockKey = "steering_lock";

    public static WheelSchema Load(string path)
    {
        var schema = JsonSerializer.Deserialize<WheelSchema>(File.ReadAllBytes(path))
                     ?? throw new InvalidDataException($"{path} is empty.");
        if (schema.Version != 1) throw new InvalidDataException($"{path}: unknown wheel schema version {schema.Version}.");
        return schema;
    }
}
