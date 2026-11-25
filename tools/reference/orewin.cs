// orewin: photographs and drives the window of the original game on Windows,
// so that the port's pictures can be compared with the original's
// (tools/reference/README.md).
//
// It needs nothing installed. Build it with the compiler that ships with
// Windows:
//
//   C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe /nologo /optimize
//       /out:orewin.exe /r:System.Drawing.dll orewin.cs
//
// That compiler speaks C# 5, so this file stays within it.
//
// Positions are game coordinates (800x600, as in the port's input files) and
// are scaled to the window's client area. Pictures are saved at the size of
// the client area.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Imaging;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

static class OreWin
{
    // ---------------------------------------------------------------- Win32

    delegate bool EnumProc(IntPtr hwnd, IntPtr lparam);

    [StructLayout(LayoutKind.Sequential)]
    struct RECT { public int left, top, right, bottom; }

    [StructLayout(LayoutKind.Sequential)]
    struct POINT { public int x, y; }

    [StructLayout(LayoutKind.Sequential)]
    struct MOUSEINPUT
    {
        public int dx, dy;
        public uint mouseData, dwFlags, time;
        public IntPtr dwExtraInfo;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct KEYBDINPUT
    {
        public ushort wVk, wScan;
        public uint dwFlags, time;
        public IntPtr dwExtraInfo;
    }

    [StructLayout(LayoutKind.Explicit)]
    struct INPUTUNION
    {
        [FieldOffset(0)] public MOUSEINPUT mi;
        [FieldOffset(0)] public KEYBDINPUT ki;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct INPUT
    {
        public uint type;
        public INPUTUNION u;
    }

    const uint INPUT_MOUSE = 0, INPUT_KEYBOARD = 1;
    const uint MOUSEEVENTF_LEFTDOWN = 0x2, MOUSEEVENTF_LEFTUP = 0x4;
    const uint MOUSEEVENTF_RIGHTDOWN = 0x8, MOUSEEVENTF_RIGHTUP = 0x10, MOUSEEVENTF_WHEEL = 0x800;
    const uint KEYEVENTF_EXTENDEDKEY = 0x1, KEYEVENTF_KEYUP = 0x2, KEYEVENTF_SCANCODE = 0x8;
    const int SW_RESTORE = 9;
    const ushort VK_MENU = 0x12;

    [DllImport("user32.dll")] static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback, IntPtr lparam);
    [DllImport("user32.dll")] static extern bool IsWindow(IntPtr hwnd);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] static extern bool IsIconic(IntPtr hwnd);
    [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr hwnd, int command);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int max);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern int GetClassName(IntPtr hwnd, StringBuilder text, int max);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")] static extern bool ClientToScreen(IntPtr hwnd, ref POINT point);
    [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll", SetLastError = true)]
    static extern uint SendInput(uint count, INPUT[] inputs, int size);
    [DllImport("user32.dll")] static extern uint MapVirtualKey(uint code, uint type);

    // ------------------------------------------------------------- settings

    static string processName = "ORE_TUBA";
    static string titlePart = null;
    static int gameWidth = 800, gameHeight = 600;
    static bool focus = true;
    static IntPtr window = IntPtr.Zero;

    sealed class Options
    {
        public readonly List<string> Positional = new List<string>();
        readonly Dictionary<string, string> named = new Dictionary<string, string>();

        public Options(string[] args, int from, string[] flags)
        {
            for (int i = from; i < args.Length; ++i)
            {
                string a = args[i];
                if (!a.StartsWith("--", StringComparison.Ordinal)) { Positional.Add(a); continue; }
                string key = a.Substring(2);
                if (Array.IndexOf(flags, key) >= 0) named[key] = "1";
                else if (i + 1 < args.Length) named[key] = args[++i];
                else throw new ArgumentException("option " + a + " needs a value");
            }
        }

        public bool Has(string key) { return named.ContainsKey(key); }
        public string Text(string key, string fallback) { return named.ContainsKey(key) ? named[key] : fallback; }
        public int Int(string key, int fallback)
        {
            return named.ContainsKey(key) ? int.Parse(named[key], CultureInfo.InvariantCulture) : fallback;
        }
        public double Number(string key, double fallback)
        {
            return named.ContainsKey(key) ? double.Parse(named[key], CultureInfo.InvariantCulture) : fallback;
        }
        public int PositionalInt(int index)
        {
            if (index >= Positional.Count) throw new ArgumentException("a number is missing");
            return int.Parse(Positional[index], CultureInfo.InvariantCulture);
        }
        public string PositionalText(int index)
        {
            if (index >= Positional.Count) throw new ArgumentException("an argument is missing");
            return Positional[index];
        }
    }

    // --------------------------------------------------------------- window

    sealed class Found
    {
        public IntPtr Handle;
        public uint Pid;
        public string Process, Title, Class;
        public int Width, Height;
    }

    static List<Found> TopWindows()
    {
        var names = new Dictionary<uint, string>();
        foreach (Process p in Process.GetProcesses())
        {
            try { names[(uint)p.Id] = p.ProcessName; } catch (Exception) { }
            p.Dispose();
        }
        var list = new List<Found>();
        EnumWindows(delegate(IntPtr hwnd, IntPtr unused)
        {
            if (!IsWindowVisible(hwnd)) return true;
            RECT r;
            if (!GetClientRect(hwnd, out r)) return true;
            var f = new Found();
            f.Handle = hwnd;
            f.Width = r.right - r.left;
            f.Height = r.bottom - r.top;
            if (f.Width <= 0 || f.Height <= 0) return true;
            GetWindowThreadProcessId(hwnd, out f.Pid);
            f.Process = names.ContainsKey(f.Pid) ? names[f.Pid] : "?";
            var text = new StringBuilder(512);
            GetWindowText(hwnd, text, text.Capacity);
            f.Title = text.ToString();
            text.Length = 0;
            GetClassName(hwnd, text, text.Capacity);
            f.Class = text.ToString();
            list.Add(f);
            return true;
        }, IntPtr.Zero);
        return list;
    }

    static bool Matches(Found f)
    {
        if (titlePart != null) return f.Title.IndexOf(titlePart, StringComparison.OrdinalIgnoreCase) >= 0;
        return f.Process.IndexOf(processName, StringComparison.OrdinalIgnoreCase) >= 0;
    }

    static Found FindGame()
    {
        Found best = null;
        foreach (Found f in TopWindows())
            if (Matches(f) && (best == null || f.Width * f.Height > best.Width * best.Height)) best = f;
        return best;
    }

    static void NeedGame()
    {
        Found f = FindGame();
        if (f == null)
            throw new InvalidOperationException("the game's window was not found (process " + processName +
                                                "); 'orewin info --all' lists the windows there are");
        window = f.Handle;
        if (IsIconic(window)) { ShowWindow(window, SW_RESTORE); Thread.Sleep(300); }
        if (focus) Focus();
    }

    static void Focus()
    {
        if (GetForegroundWindow() == window) return;
        // Windows lets a background program raise a window only right after
        // input of its own; a tap on Alt is the usual way to have had some.
        SendKey(VK_MENU, true);
        SendKey(VK_MENU, false);
        SetForegroundWindow(window);
        Thread.Sleep(200);
    }

    static Rectangle ClientOnScreen()
    {
        if (!IsWindow(window)) throw new InvalidOperationException("the game's window is gone");
        RECT r;
        GetClientRect(window, out r);
        var origin = new POINT();
        ClientToScreen(window, ref origin);
        return new Rectangle(origin.x, origin.y, r.right - r.left, r.bottom - r.top);
    }

    // ---------------------------------------------------------------- input

    static void Send(INPUT input)
    {
        if (SendInput(1, new INPUT[] { input }, Marshal.SizeOf(typeof(INPUT))) != 1)
            throw new InvalidOperationException("SendInput was refused (error " + Marshal.GetLastWin32Error() +
                                                "); is the game running with administrator rights?");
    }

    static void SendMouse(uint flags, int data)
    {
        var input = new INPUT();
        input.type = INPUT_MOUSE;
        input.u.mi.dwFlags = flags;
        input.u.mi.mouseData = unchecked((uint)data);
        Send(input);
    }

    static bool IsExtendedKey(ushort vk)
    {
        // The arrow block, Insert, Delete, Home, End, Page Up and Down, right Ctrl and Alt.
        return (vk >= 0x21 && vk <= 0x28) || vk == 0x2D || vk == 0x2E || vk == 0xA3 || vk == 0xA5;
    }

    static void SendKey(ushort vk, bool down)
    {
        var input = new INPUT();
        input.type = INPUT_KEYBOARD;
        input.u.ki.wVk = vk;
        input.u.ki.wScan = (ushort)MapVirtualKey(vk, 0);
        input.u.ki.dwFlags = (down ? 0 : KEYEVENTF_KEYUP) | (IsExtendedKey(vk) ? KEYEVENTF_EXTENDEDKEY : 0);
        Send(input);
    }

    static void MoveTo(int gameX, int gameY)
    {
        Rectangle c = ClientOnScreen();
        SetCursorPos(c.X + (int)((gameX + 0.5) * c.Width / gameWidth), c.Y + (int)((gameY + 0.5) * c.Height / gameHeight));
    }

    static void Click(int gameX, int gameY, bool right, int holdMs)
    {
        MoveTo(gameX, gameY);
        Thread.Sleep(30);
        SendMouse(right ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN, 0);
        Thread.Sleep(holdMs);
        SendMouse(right ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP, 0);
    }

    // ------------------------------------------------------------- pictures

    sealed class Picture
    {
        public int Width, Height;
        public int[] Pixels;   // 0x00RRGGBB, row by row
    }

    static Picture Capture()
    {
        Rectangle c = ClientOnScreen();
        if (c.Width <= 0 || c.Height <= 0) throw new InvalidOperationException("the game's window has no area");
        var p = new Picture();
        p.Width = c.Width;
        p.Height = c.Height;
        p.Pixels = new int[c.Width * c.Height];
        using (var bitmap = new Bitmap(c.Width, c.Height, PixelFormat.Format32bppRgb))
        {
            using (Graphics g = Graphics.FromImage(bitmap))
                g.CopyFromScreen(c.X, c.Y, 0, 0, c.Size, CopyPixelOperation.SourceCopy);
            BitmapData data = bitmap.LockBits(new Rectangle(0, 0, c.Width, c.Height), ImageLockMode.ReadOnly,
                                              PixelFormat.Format32bppRgb);
            try
            {
                for (int y = 0; y < c.Height; ++y)
                    Marshal.Copy(new IntPtr(data.Scan0.ToInt64() + (long)y * data.Stride), p.Pixels, y * c.Width, c.Width);
            }
            finally { bitmap.UnlockBits(data); }
        }
        for (int i = 0; i < p.Pixels.Length; ++i) p.Pixels[i] &= 0xFFFFFF;
        return p;
    }

    static void Save(Picture p, string path)
    {
        using (var bitmap = new Bitmap(p.Width, p.Height, PixelFormat.Format32bppRgb))
        {
            BitmapData data = bitmap.LockBits(new Rectangle(0, 0, p.Width, p.Height), ImageLockMode.WriteOnly,
                                              PixelFormat.Format32bppRgb);
            try
            {
                for (int y = 0; y < p.Height; ++y)
                    Marshal.Copy(p.Pixels, y * p.Width, new IntPtr(data.Scan0.ToInt64() + (long)y * data.Stride), p.Width);
            }
            finally { bitmap.UnlockBits(data); }
            bitmap.Save(path, ImageFormat.Png);
        }
    }

    // The share of pixels (0..1) of which a colour channel differs by more
    // than 8; 1 when the sizes differ.
    static double Changed(Picture a, Picture b)
    {
        if (a.Width != b.Width || a.Height != b.Height) return 1;
        int count = 0;
        for (int i = 0; i < a.Pixels.Length; ++i)
        {
            int x = a.Pixels[i], y = b.Pixels[i];
            if (x == y) continue;
            if (Math.Abs(((x >> 16) & 255) - ((y >> 16) & 255)) > 8 || Math.Abs(((x >> 8) & 255) - ((y >> 8) & 255)) > 8 ||
                Math.Abs((x & 255) - (y & 255)) > 8)
                ++count;
        }
        return (double)count / a.Pixels.Length;
    }

    static uint Checksum(Picture p)
    {
        uint h = 2166136261;
        for (int i = 0; i < p.Pixels.Length; ++i) h = (h ^ (uint)p.Pixels[i]) * 16777619;
        return h;
    }

    sealed class Rest
    {
        public Picture Picture;
        public bool Still;      // false: the time ran out while the picture still moved
        public bool Reacted;    // the picture left what it was before
        public long WaitedMs;
    }

    // Waits until the picture stands still: `calm` captures in a row, `interval`
    // ms apart, each differing from the one before by no more than `tolerance`.
    // With `before` given, the picture must first leave it (or `reactMs` pass),
    // so that the game's delay after a click is not taken for standing still.
    static Rest WaitStill(Picture before, int timeoutMs, int intervalMs, double tolerance, int calm, int reactMs)
    {
        var watch = Stopwatch.StartNew();
        var rest = new Rest();
        rest.Reacted = before == null;
        Picture last = Capture();
        int run = 0;
        for (;;)
        {
            Thread.Sleep(intervalMs);
            Picture now = Capture();
            if (!rest.Reacted)
            {
                if (Changed(before, now) > tolerance) rest.Reacted = true;
                else if (watch.ElapsedMilliseconds < reactMs) { last = now; continue; }
            }
            run = Changed(last, now) <= tolerance ? run + 1 : 0;
            last = now;
            if (run >= calm) { rest.Still = true; break; }
            if (watch.ElapsedMilliseconds >= timeoutMs) break;
        }
        rest.Picture = last;
        rest.WaitedMs = watch.ElapsedMilliseconds;
        return rest;
    }

    // ------------------------------------------------------------- commands

    static int Info(Options o)
    {
        bool all = o.Has("all");
        int shown = 0;
        foreach (Found f in TopWindows())
        {
            if (!all && !Matches(f)) continue;
            Console.WriteLine("{0}\tpid {1}\tclient {2}x{3}\tclass {4}\t{5}", f.Process, f.Pid, f.Width, f.Height, f.Class,
                              f.Title);
            ++shown;
        }
        if (shown == 0) Console.WriteLine("no window of process " + processName + "; try --all");
        return shown == 0 ? 1 : 0;
    }

    static int Walk(Options o)
    {
        int count = o.PositionalInt(0);
        string dir = o.PositionalText(1);
        int x = o.Int("x", 400), y = o.Int("y", 500), first = o.Int("start", 0);
        int timeout = o.Int("timeout", 8000), interval = o.Int("interval", 120), calm = o.Int("calm", 3);
        int react = o.Int("react", 1500), hold = o.Int("hold", 50);
        double tolerance = o.Number("tolerance", 0.002);
        Directory.CreateDirectory(dir);
        string stop = Path.Combine(dir, "stop");
        string log = Path.Combine(dir, "walk.csv");
        if (!File.Exists(log)) File.WriteAllText(log, "index,at_ms,waited_ms,still,reacted,checksum\n");
        var watch = Stopwatch.StartNew();
        Picture before = null;
        for (int i = first; i < first + count; ++i)
        {
            if (File.Exists(stop)) { Console.WriteLine("stopped by the file 'stop' at " + i); break; }
            Rest rest = WaitStill(before, timeout, interval, tolerance, calm, react);
            Save(rest.Picture, Path.Combine(dir, string.Format(CultureInfo.InvariantCulture, "line_{0:D5}.png", i)));
            File.AppendAllText(log, string.Format(CultureInfo.InvariantCulture, "{0},{1},{2},{3},{4},{5:x8}\n", i,
                                                  watch.ElapsedMilliseconds, rest.WaitedMs, rest.Still ? 1 : 0,
                                                  rest.Reacted ? 1 : 0, Checksum(rest.Picture)));
            before = rest.Picture;
            if (focus) Focus();
            Click(x, y, false, hold);
        }
        Console.WriteLine("walk ended after " + watch.ElapsedMilliseconds + " ms");
        return 0;
    }

    static int Burst(Options o)
    {
        double seconds = double.Parse(o.PositionalText(0), CultureInfo.InvariantCulture);
        string dir = o.PositionalText(1);
        int fps = o.Int("fps", 30);
        Directory.CreateDirectory(dir);
        var queue = new Queue<KeyValuePair<string, Picture>>();
        bool done = false;
        var writer = new Thread(delegate()
        {
            for (;;)
            {
                KeyValuePair<string, Picture> item;
                lock (queue)
                {
                    while (queue.Count == 0 && !done) Monitor.Wait(queue);
                    if (queue.Count == 0) return;
                    item = queue.Dequeue();
                }
                Save(item.Value, item.Key);
            }
        });
        writer.Start();
        var times = new StringBuilder("index,at_ms\n");
        var watch = Stopwatch.StartNew();
        if (o.Has("click-x")) Click(o.Int("click-x", 400), o.Int("click-y", 500), false, 50);
        else if (o.Has("key")) { SendKey((ushort)o.Int("key", 13), true); Thread.Sleep(50); SendKey((ushort)o.Int("key", 13), false); }
        int n = 0;
        while (watch.Elapsed.TotalSeconds < seconds)
        {
            long due = (long)(n * 1000.0 / fps);
            long wait = due - watch.ElapsedMilliseconds;
            if (wait > 0) Thread.Sleep((int)wait);
            long at = watch.ElapsedMilliseconds;
            Picture p = Capture();
            string name = Path.Combine(dir, string.Format(CultureInfo.InvariantCulture, "burst_{0:D5}.png", n));
            times.AppendFormat(CultureInfo.InvariantCulture, "{0},{1}\n", n, at);
            lock (queue) { queue.Enqueue(new KeyValuePair<string, Picture>(name, p)); Monitor.Pulse(queue); }
            ++n;
        }
        lock (queue) { done = true; Monitor.PulseAll(queue); }
        writer.Join();
        File.WriteAllText(Path.Combine(dir, "burst.csv"), times.ToString());
        Console.WriteLine("burst: " + n + " pictures in " + watch.ElapsedMilliseconds + " ms");
        return 0;
    }

    // Plays an input file of the port's test host (engine/host/main.mm) in real
    // time, 60 frames to the second.
    static int Run(Options o)
    {
        string file = o.PositionalText(0);
        string dir = o.PositionalText(1);
        int shotEvery = o.Int("shot-every", 0);
        Directory.CreateDirectory(dir);
        var commands = new List<KeyValuePair<int, string[]>>();
        int lastFrame = 0;
        foreach (string raw in File.ReadAllLines(file))
        {
            string line = raw.Trim();
            if (line.Length == 0 || line[0] == '#') continue;
            string[] parts = line.Split(new char[] { ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries);
            int frame = int.Parse(parts[0], CultureInfo.InvariantCulture);
            commands.Add(new KeyValuePair<int, string[]>(frame, parts));
            if (frame > lastFrame) lastFrame = frame;
        }
        commands.Sort(delegate(KeyValuePair<int, string[]> a, KeyValuePair<int, string[]> b) { return a.Key.CompareTo(b.Key); });
        int frames = o.Int("frames", lastFrame + 60);
        var watch = Stopwatch.StartNew();
        int next = 0;
        for (int f = 0; f < frames; ++f)
        {
            long wait = (long)(f * 1000.0 / 60) - watch.ElapsedMilliseconds;
            if (wait > 0) Thread.Sleep((int)wait);
            for (; next < commands.Count && commands[next].Key <= f; ++next)
            {
                string[] c = commands[next].Value;
                string op = c[1];
                if (op == "click" || op == "rclick") Click(int.Parse(c[2]), int.Parse(c[3]), op == "rclick", 50);
                else if (op == "move") MoveTo(int.Parse(c[2]), int.Parse(c[3]));
                else if (op == "wheel") SendMouse(MOUSEEVENTF_WHEEL, int.Parse(c[2]));
                else if (op == "key")
                {
                    ushort vk = (ushort)int.Parse(c[2]);
                    string state = c.Length > 3 ? c[3] : "";
                    if (state == "down") SendKey(vk, true);
                    else if (state == "up") SendKey(vk, false);
                    else { SendKey(vk, true); Thread.Sleep(50); SendKey(vk, false); }
                }
                else if (op == "shot") Save(Capture(), Path.Combine(dir, c[2] + ".png"));
                else if (op == "quit") { frames = f; break; }
                else Console.WriteLine("frame " + f + ": '" + op + "' has no meaning for the original, skipped");
            }
            if (shotEvery > 0 && f % shotEvery == 0)
                Save(Capture(), Path.Combine(dir, string.Format(CultureInfo.InvariantCulture, "f{0:D5}.png", f)));
        }
        Save(Capture(), Path.Combine(dir, "last.png"));
        Console.WriteLine("ran " + frames + " frames in " + watch.ElapsedMilliseconds + " ms");
        return 0;
    }

    static int Usage()
    {
        Console.Error.WriteLine(
            "orewin info [--all]\n" +
            "orewin shot OUT.png\n" +
            "orewin click X Y [--right] [--hold MS]\n" +
            "orewin move X Y\n" +
            "orewin wheel DELTA\n" +
            "orewin key VK [down|up] [--hold MS]\n" +
            "orewin still [OUT.png] [--timeout MS]\n" +
            "orewin walk COUNT DIR [--x 400 --y 500] [--start N] [--timeout MS]\n" +
            "orewin burst SECONDS DIR [--fps 30] [--click-x X --click-y Y | --key VK]\n" +
            "orewin run FILE.input DIR [--shot-every N] [--frames N]\n" +
            "For all: [--process NAME | --title TEXT] [--game WxH] [--no-focus]");
        return 2;
    }

    static int Main(string[] args)
    {
        if (args.Length == 0) return Usage();
        try
        {
            SetProcessDPIAware();
            var o = new Options(args, 1, new string[] { "all", "right", "no-focus" });
            processName = o.Text("process", processName);
            titlePart = o.Text("title", null);
            focus = !o.Has("no-focus");
            string[] size = o.Text("game", "800x600").Split('x');
            gameWidth = int.Parse(size[0], CultureInfo.InvariantCulture);
            gameHeight = int.Parse(size[1], CultureInfo.InvariantCulture);
            string command = args[0];
            if (command == "info") return Info(o);
            NeedGame();
            if (command == "shot")
            {
                Picture p = Capture();
                Save(p, o.PositionalText(0));
                Console.WriteLine("{0}x{1} checksum {2:x8}", p.Width, p.Height, Checksum(p));
            }
            else if (command == "click") Click(o.PositionalInt(0), o.PositionalInt(1), o.Has("right"), o.Int("hold", 50));
            else if (command == "move") MoveTo(o.PositionalInt(0), o.PositionalInt(1));
            else if (command == "wheel") SendMouse(MOUSEEVENTF_WHEEL, o.PositionalInt(0));
            else if (command == "key")
            {
                ushort vk = (ushort)o.PositionalInt(0);
                string state = o.Positional.Count > 1 ? o.Positional[1] : "";
                if (state == "down") SendKey(vk, true);
                else if (state == "up") SendKey(vk, false);
                else { SendKey(vk, true); Thread.Sleep(o.Int("hold", 50)); SendKey(vk, false); }
            }
            else if (command == "still")
            {
                Rest rest = WaitStill(null, o.Int("timeout", 8000), o.Int("interval", 120), o.Number("tolerance", 0.002),
                                      o.Int("calm", 3), 0);
                if (o.Positional.Count > 0) Save(rest.Picture, o.Positional[0]);
                Console.WriteLine("{0} after {1} ms, checksum {2:x8}", rest.Still ? "still" : "moving", rest.WaitedMs,
                                  Checksum(rest.Picture));
                return rest.Still ? 0 : 3;
            }
            else if (command == "walk") return Walk(o);
            else if (command == "burst") return Burst(o);
            else if (command == "run") return Run(o);
            else return Usage();
            return 0;
        }
        catch (Exception e)
        {
            Console.Error.WriteLine("orewin: " + e.Message);
            return 1;
        }
    }
}
