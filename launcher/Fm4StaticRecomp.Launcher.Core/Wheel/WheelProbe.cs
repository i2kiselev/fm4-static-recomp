// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using System.Text.Json;

namespace Fm4StaticRecomp.Launcher.Core.Wheel;

public sealed record ProbeDevice(uint Id, string Name, string Path, ushort Vendor, ushort Product, bool Wheel,
    IReadOnlyList<short> Rest, int Buttons, int Hats)
{
    public string Ids => $"{Vendor:x4}:{Product:x4}";
}

public sealed record ProbeState(uint Id, IReadOnlyList<short> Axes, IReadOnlyList<bool> Buttons, IReadOnlyList<byte> Hats);

public sealed record ProbeRemoved(uint Id);

public sealed record ProbeReady;

// The game exe in its joystick probe mode (--fm4_wheel_probe): SDL as the game itself numbers the
// devices, streamed as JSON lines (the SDK's joystick_probe.cpp). Events come on a worker thread.
public sealed class WheelProbe(Func<Process?> start, TimeSpan? readyTimeout = null) : IDisposable
{
    public const string UpdateMessage = "Update the game to set up a wheel.";
    public const string StoppedMessage = "The wheel reader stopped. Open WHEEL again.";

    private Process? _process;
    private Timer? _readyTimer;
    private volatile bool _ready, _disposed;
    private int _ended;

    public event Action<ProbeDevice>? DeviceAdded;
    public event Action<uint>? DeviceRemoved;
    public event Action<ProbeState>? StateChanged;
    public event Action? Ready;
    public event Action<string?>? Ended;

    public static ProcessStartInfo StartInfo(string executable, IEnumerable<string> arguments)
    {
        var info = new ProcessStartInfo(executable)
        {
            UseShellExecute = false,
            RedirectStandardInput = true,
            RedirectStandardOutput = true,
            CreateNoWindow = true,
            WorkingDirectory = System.IO.Path.GetDirectoryName(executable)!,
        };
        foreach (var argument in arguments) info.ArgumentList.Add(argument);
        return info;
    }

    public void Start()
    {
        try
        {
            _process = start();
        }
        catch (Exception error) when (error is Win32Exception or IOException or InvalidOperationException)
        {
            _process = null;
        }
        if (_process is null)
        {
            End(UpdateMessage);
            return;
        }
        // A game too old for the probe mode may start the game itself instead of quitting.
        _readyTimer = new Timer(_ =>
        {
            if (_ready || _disposed) return;
            End(UpdateMessage);
            Kill();
        }, null, readyTimeout ?? TimeSpan.FromSeconds(10), Timeout.InfiniteTimeSpan);
        var process = _process;
        Task.Run(() => Read(process));
    }

    public static IEnumerable<object> ParseLines(TextReader reader)
    {
        while (reader.ReadLine() is { } line)
            if (ParseLine(line) is { } item) yield return item;
    }

    // Closing stdin is the probe's signal to quit.
    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _readyTimer?.Dispose();
        if (_process is not { } process) return;
        try
        {
            process.StandardInput.Close();
            if (!process.WaitForExit(2000)) Kill();
        }
        catch (Exception error) when (error is InvalidOperationException or IOException or Win32Exception) { }
        process.Dispose();
    }

    private void Read(Process process)
    {
        try
        {
            foreach (var item in ParseLines(process.StandardOutput))
            {
                if (_disposed) return;
                switch (item)
                {
                    case ProbeDevice device: DeviceAdded?.Invoke(device); break;
                    case ProbeState state: StateChanged?.Invoke(state); break;
                    case ProbeRemoved removed: DeviceRemoved?.Invoke(removed.Id); break;
                    case ProbeReady:
                        _ready = true;
                        Ready?.Invoke();
                        break;
                }
            }
        }
        catch (Exception error) when (error is IOException or ObjectDisposedException or InvalidOperationException) { }
        if (!_disposed) End(_ready ? StoppedMessage : UpdateMessage);
    }

    private void End(string reason)
    {
        if (Interlocked.Exchange(ref _ended, 1) == 0) Ended?.Invoke(reason);
    }

    private void Kill()
    {
        try { _process?.Kill(entireProcessTree: true); }
        catch (Exception error) when (error is InvalidOperationException or Win32Exception) { }
    }

    private static object? ParseLine(string line)
    {
        try
        {
            using var document = JsonDocument.Parse(line);
            var root = document.RootElement;
            if (root.ValueKind != JsonValueKind.Object) return null;
            if (root.TryGetProperty("device", out var d))
                return new ProbeDevice(d.GetProperty("id").GetUInt32(), d.GetProperty("name").GetString()!,
                    d.GetProperty("path").GetString()!, Hex(d.GetProperty("vendor")), Hex(d.GetProperty("product")),
                    d.GetProperty("wheel").GetBoolean(), d.GetProperty("rest").EnumerateArray().Select(a => a.GetInt16()).ToList(),
                    d.GetProperty("buttons").GetInt32(), d.GetProperty("hats").GetInt32());
            if (root.TryGetProperty("state", out var s))
                return new ProbeState(s.GetProperty("id").GetUInt32(),
                    s.GetProperty("axes").EnumerateArray().Select(a => a.GetInt16()).ToList(),
                    s.GetProperty("buttons").GetString()!.Select(c => c == '1').ToList(),
                    s.GetProperty("hats").EnumerateArray().Select(h => h.GetByte()).ToList());
            if (root.TryGetProperty("removed", out var r)) return new ProbeRemoved(r.GetUInt32());
            if (root.TryGetProperty("ready", out var ready) && ready.ValueKind == JsonValueKind.True) return new ProbeReady();
        }
        catch (Exception error) when (error is JsonException or InvalidOperationException or FormatException
                                          or KeyNotFoundException or OverflowException) { }
        return null;
    }

    private static ushort Hex(JsonElement element) =>
        ushort.Parse(element.GetString()!, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture);
}
