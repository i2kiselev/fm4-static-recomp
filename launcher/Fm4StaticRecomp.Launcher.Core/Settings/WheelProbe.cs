// Copyright (c) 2026, FugaziScript
// BSD 3-Clause License: see LICENSE in the repository root.
using System.Diagnostics;
using System.Text.Json;

namespace Fm4StaticRecomp.Launcher.Core.Settings;

public sealed record WheelDevice(string Match, string Name, string Path, string Ids, bool Wheel,
    IReadOnlyList<int> Axes, IReadOnlyList<int> Rest, IReadOnlyList<bool> Buttons, IReadOnlyList<int> Hats);

// What the game would read: steering -32768..32767, pedals 0..255, pad button bits, the held gear.
public sealed record WheelPad(int Steering, int Throttle, int Brake, int Clutch, int Buttons, int Gear);

public sealed record WheelSnapshot(bool Ready, string Status, string ForceDevice, bool TestBusy, string Base,
    IReadOnlyList<WheelDevice> Devices, WheelPad Pad)
{
    public static WheelSnapshot? Parse(string line)
    {
        if (!line.StartsWith("{\"ready\"", StringComparison.Ordinal)) return null;
        try
        {
            using var document = JsonDocument.Parse(line);
            var root = document.RootElement;
            var devices = root.GetProperty("devices").EnumerateArray().Select(d => new WheelDevice(
                d.GetProperty("match").GetString() ?? "", d.GetProperty("name").GetString() ?? "",
                d.GetProperty("path").GetString() ?? "", d.GetProperty("ids").GetString() ?? "",
                d.GetProperty("wheel").GetBoolean(), Ints(d.GetProperty("axes")), Ints(d.GetProperty("rest")),
                Ints(d.GetProperty("buttons")).Select(b => b != 0).ToList(), Ints(d.GetProperty("hats")))).ToList();
            var pad = root.GetProperty("pad");
            return new WheelSnapshot(root.GetProperty("ready").GetBoolean(), root.GetProperty("status").GetString() ?? "",
                root.GetProperty("ffb_device").GetString() ?? "", root.GetProperty("test_busy").GetBoolean(),
                root.GetProperty("base").GetString() ?? "", devices,
                new WheelPad(pad.GetProperty("steering").GetInt32(), pad.GetProperty("throttle").GetInt32(),
                    pad.GetProperty("brake").GetInt32(), pad.GetProperty("clutch").GetInt32(),
                    pad.GetProperty("buttons").GetInt32(), pad.GetProperty("gear").GetInt32()));
        }
        catch (Exception error) when (error is JsonException or KeyNotFoundException or InvalidOperationException)
        {
            return null;
        }
    }

    private static List<int> Ints(JsonElement array) => array.EnumerateArray().Select(e => e.GetInt32()).ToList();
}

// The game's --fm4_wheel_probe (src\wheel\wheel_probe.cpp), running while the WHEEL panel is open: the
// connected controllers through the game's own SDL and bindings. It sends no force until Test, so the
// game can take the force device once the panel closes.
public sealed class WheelProbe : IDisposable
{
    private readonly Process _process;
    private readonly object _lock = new();
    private bool _disposed;

    public event Action<WheelSnapshot>? Updated;
    public event Action? Exited;

    private WheelProbe(Process process) => _process = process;

    private static readonly Dictionary<(string, DateTime), bool> SupportCache = [];

    // Whether this build of the game has --fm4_wheel_probe: an older one would ignore the option and
    // start the game. Looks for the probe's marker (src\wheel\wheel_probe.cpp) in the executable, once
    // per build; the option's own name is too short to be kept as text.
    public static bool Supported(string gameExecutable)
    {
        if (!File.Exists(gameExecutable)) return false;
        var key = (gameExecutable, File.GetLastWriteTimeUtc(gameExecutable));
        lock (SupportCache)
        {
            if (SupportCache.TryGetValue(key, out var known)) return known;
        }
        var needle = "fm4-wheel-probe/1"u8.ToArray();
        var found = false;
        try
        {
            using var stream = File.OpenRead(gameExecutable);
            var buffer = new byte[1 << 20];
            var carry = 0;
            int read;
            while (!found && (read = stream.Read(buffer, carry, buffer.Length - carry)) > 0)
            {
                var length = carry + read;
                found = buffer.AsSpan(0, length).IndexOf(needle) >= 0;
                carry = Math.Min(needle.Length - 1, length);
                Array.Copy(buffer, length - carry, buffer, 0, carry);
            }
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            return false;
        }
        lock (SupportCache) SupportCache[key] = found;
        return found;
    }

    public static WheelProbe Start(string gameExecutable, string workingDirectory, IEnumerable<string> arguments)
    {
        var start = new ProcessStartInfo(gameExecutable)
        {
            UseShellExecute = false, CreateNoWindow = true, WorkingDirectory = workingDirectory,
            RedirectStandardInput = true, RedirectStandardOutput = true, RedirectStandardError = true,
        };
        start.ArgumentList.Add("--fm4_wheel_probe=true");
        foreach (var argument in arguments) start.ArgumentList.Add(argument);
        var process = new Process { StartInfo = start, EnableRaisingEvents = true };
        var probe = new WheelProbe(process);
        process.OutputDataReceived += (_, e) =>
        {
            if (e.Data is { } line && WheelSnapshot.Parse(line) is { } snapshot) probe.Updated?.Invoke(snapshot);
        };
        process.ErrorDataReceived += (_, _) => { };
        process.Exited += (_, _) => probe.Exited?.Invoke();
        process.Start();
        process.BeginOutputReadLine();
        process.BeginErrorReadLine();
        return probe;
    }

    // An fm4_wheel_* value for the live view and the next test (not saved: WheelSettings saves).
    public void Set(string key, string value) => Send($"set {key} {value}");

    public void Test() => Send("test");

    public void TestRumble() => Send("testrumble");

    private void Send(string line)
    {
        lock (_lock)
        {
            if (_disposed || _process.HasExited) return;
            try
            {
                _process.StandardInput.WriteLine(line);
                _process.StandardInput.Flush();
            }
            catch (IOException) { }
        }
    }

    public void Dispose()
    {
        lock (_lock)
        {
            if (_disposed) return;
            _disposed = true;
            try
            {
                _process.StandardInput.WriteLine("quit");
                _process.StandardInput.Close();
                if (!_process.WaitForExit(1500)) _process.Kill(entireProcessTree: true);
            }
            catch (Exception error) when (error is IOException or InvalidOperationException
                                              or System.ComponentModel.Win32Exception) { }
            _process.Dispose();
        }
    }
}
