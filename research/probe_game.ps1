# Launches a game, records what its windows look like (styles, rects, DPI context, display
# mode), saves screenshots at intervals, then kills it. Used to observe compatibility problems.
param(
    [Parameter(Mandatory)] [string]$Exe,
    [string]$OutDir = "$env:TEMP\hogfixer_probe",
    [int]$Seconds = 20,
    [int[]]$ShotsAt = @(8, 15, 19),
    [string]$Arguments = "",
    [switch]$KeepRunning,
    [string]$InjectDll = ""
)
$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force $OutDir | Out-Null

Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Text;
public static class Probe {
    [DllImport("user32.dll")] public static extern IntPtr SetProcessDpiAwarenessContext(IntPtr v);
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern int GetWindowLong(IntPtr h, int i);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
    [DllImport("user32.dll")] public static extern IntPtr GetWindowDpiAwarenessContext(IntPtr h);
    [DllImport("user32.dll")] public static extern int GetAwarenessFromDpiAwarenessContext(IntPtr c);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern bool EnumDisplaySettings(string dev, int mode, ref DEVMODE dm);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; public override string ToString(){ return string.Format("({0},{1})-({2},{3}) {4}x{5}", L,T,R,B,R-L,B-T);} }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)] public struct DEVMODE {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst=32)] public string dmDeviceName;
        public short dmSpecVersion, dmDriverVersion, dmSize, dmDriverExtra; public int dmFields;
        public int dmPositionX, dmPositionY, dmDisplayOrientation, dmDisplayFixedOutput;
        public short dmColor, dmDuplex, dmYResolution, dmTTOption, dmCollate;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst=32)] public string dmFormName;
        public short dmLogPixels; public int dmBitsPerPel, dmPelsWidth, dmPelsHeight, dmDisplayFlags, dmDisplayFrequency;
        public int dmICMMethod, dmICMIntent, dmMediaType, dmDitherType, dmReserved1, dmReserved2, dmPanningWidth, dmPanningHeight; }
    public static List<IntPtr> WindowsOf(uint pid) {
        var l = new List<IntPtr>();
        EnumWindows((h, x) => { uint p; GetWindowThreadProcessId(h, out p); if (p == pid && IsWindowVisible(h)) l.Add(h); return true; }, IntPtr.Zero);
        return l;
    }
    public static string Describe(IntPtr h) {
        var c = new StringBuilder(256); GetClassName(h, c, 256);
        var t = new StringBuilder(256); GetWindowText(h, t, 256);
        RECT wr, cr; GetWindowRect(h, out wr); GetClientRect(h, out cr);
        POINT o = new POINT(); ClientToScreen(h, ref o);
        int aw = GetAwarenessFromDpiAwarenessContext(GetWindowDpiAwarenessContext(h));
        return string.Format("hwnd={0:X} class='{1}' title='{2}' style={3:X8} ex={4:X8} window={5} client={6} clientOrigin=({7},{8}) dpiAwareness={9} dpi={10}",
            h.ToInt64(), c, t, GetWindowLong(h, -16), GetWindowLong(h, -20), wr, cr, o.X, o.Y, aw, GetDpiForWindow(h));
    }
    public static string Modes() {
        var sb = new StringBuilder();
        foreach (var d in new[]{ @"\\.\DISPLAY1", @"\\.\DISPLAY2" }) {
            var dm = new DEVMODE(); dm.dmSize = (short)Marshal.SizeOf(typeof(DEVMODE));
            if (EnumDisplaySettings(d, -1, ref dm)) sb.AppendFormat("{0}: {1}x{2}@{3} pos=({4},{5}); ", d, dm.dmPelsWidth, dm.dmPelsHeight, dm.dmDisplayFrequency, dm.dmPositionX, dm.dmPositionY);
        }
        return sb.ToString();
    }
    public static void ShotScreen(IntPtr h, string path) {
        RECT r; GetWindowRect(h, out r);
        int w = Math.Max(1, r.R - r.L), hh = Math.Max(1, r.B - r.T);
        using (var bmp = new Bitmap(w, hh)) using (var g = Graphics.FromImage(bmp)) {
            g.CopyFromScreen(r.L, r.T, 0, 0, new Size(w, hh)); bmp.Save(path, ImageFormat.Png); }
    }
}
"@
[Probe]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null   # per-monitor v2: physical pixels

$log = Join-Path $OutDir ("probe_" + [IO.Path]::GetFileNameWithoutExtension($Exe) + ".log")
"=== $Exe  $(Get-Date)" | Out-File $log
"modes before: " + [Probe]::Modes() | Tee-Object -FilePath $log -Append

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $Exe; $psi.WorkingDirectory = Split-Path $Exe; $psi.Arguments = $Arguments; $psi.UseShellExecute = $false
$p = [System.Diagnostics.Process]::Start($psi)
$t0 = Get-Date
$shot = 0
while (((Get-Date) - $t0).TotalSeconds -lt $Seconds -and -not $p.HasExited) {
    Start-Sleep -Milliseconds 1000
    $el = [int]((Get-Date) - $t0).TotalSeconds
    $ws = [Probe]::WindowsOf([uint32]$p.Id)
    foreach ($w in $ws) { "t=$el " + [Probe]::Describe($w) | Tee-Object -FilePath $log -Append }
    if ($ShotsAt -contains $el -and $ws.Count -gt 0) {
        $f = Join-Path $OutDir ("shot_" + [IO.Path]::GetFileNameWithoutExtension($Exe) + "_$el.png")
        [Probe]::ShotScreen($ws[0], $f); "screenshot $f" | Tee-Object -FilePath $log -Append
        "modes at t=$el : " + [Probe]::Modes() | Tee-Object -FilePath $log -Append
    }
}
"exited=$($p.HasExited)" | Tee-Object -FilePath $log -Append
if (-not $KeepRunning -and -not $p.HasExited) { $p.Kill() }
Start-Sleep -Milliseconds 1500
"modes after: " + [Probe]::Modes() | Tee-Object -FilePath $log -Append
