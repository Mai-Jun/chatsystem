# Capture a single window of im_client.exe to PNG (ASCII only).
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File scripts/win_ui_shot.ps1 -Out <path> [-Title <regex>]
param(
  [Parameter(Mandatory=$true)][string]$Out,
  [string]$Title = ""
)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class W32 {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdcBlt, uint nFlags);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT r);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr hWnd, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lp);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int cmd);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr after, int x, int y, int w, int h, uint flags);
  public delegate bool EnumProc(IntPtr hWnd, IntPtr lp);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

$proc = Get-Process -Name im_client -ErrorAction Stop | Select-Object -First 1
[W32]::SetProcessDPIAware() | Out-Null  # physical pixel coords for GetWindowRect/CopyFromScreen
$targets = New-Object System.Collections.ArrayList
$cb = [W32+EnumProc]{
  param($h, $lp)
  $pid2 = 0
  [W32]::GetWindowThreadProcessId($h, [ref]$pid2) | Out-Null
  if ($pid2 -eq $proc.Id -and [W32]::IsWindowVisible($h)) {
    $sb = New-Object System.Text.StringBuilder 512
    [W32]::GetWindowText($h, $sb, 512) | Out-Null
    $t = $sb.ToString()
    if ($t.Length -gt 0) {
      $r = New-Object W32+RECT
      [W32]::GetWindowRect($h, [ref]$r) | Out-Null
      $w = $r.Right - $r.Left; $h2 = $r.Bottom - $r.Top
      if ($w -gt 100 -and $h2 -gt 100) { $targets.Add(@{H=$h; T=$t; W=$w; H2=$h2}) | Out-Null }
    }
  }
  return $true
}
[W32]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null

$pick = $null
if ($Title -ne "") {
  foreach ($t in $targets) { if ($t.T -match $Title) { $pick = $t; break } }
} else {
  if ($targets.Count -gt 0) { $pick = $targets[0] }
}
if ($pick -eq $null) {
  Write-Output "NO_WINDOW_FOUND (proc=$($proc.Id) windows=$($targets.Count))"
  foreach ($t in $targets) { Write-Output ("  title=" + $t.T + " size=" + $t.W + "x" + $t.H2) }
  exit 1
}

# Bring window forward, move it to a fixed anchor, temporarily pin on top, then
# capture at its real rect. (Foreground stealing is restricted on Windows, so a
# brief TOPMOST toggle is the reliable way to get an unobstructed screen copy.)
[W32]::ShowWindow($pick.H, 9) | Out-Null          # SW_RESTORE
[W32]::SetForegroundWindow($pick.H) | Out-Null
$anchorX = 80; $anchorY = 60
[W32]::SetWindowPos($pick.H, [IntPtr]::Zero, $anchorX, $anchorY, 0, 0, 0x0001 -bor 0x0010) | Out-Null  # NOSIZE|NOACTIVATE
[W32]::SetWindowPos($pick.H, [IntPtr](-1), 0, 0, 0, 0, 0x0001 -bor 0x0002 -bor 0x0010) | Out-Null   # TOPMOST|NOSIZE|NOMOVE|NOACTIVATE
Start-Sleep -Milliseconds 700
$r = New-Object W32+RECT
[W32]::GetWindowRect($pick.H, [ref]$r) | Out-Null
$w = $r.Right - $r.Left; $h2 = $r.Bottom - $r.Top
$bmp = New-Object System.Drawing.Bitmap($w, $h2)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.Left, $r.Top, 0, 0, (New-Object System.Drawing.Size($w, $h2)))
$g.Dispose()
[W32]::SetWindowPos($pick.H, [IntPtr](-2), 0, 0, 0, 0, 0x0001 -bor 0x0002 -bor 0x0010) | Out-Null   # NOTOPMOST
$dir = Split-Path $Out -Parent
if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output ("SAVED " + $Out + " title=" + $pick.T + " size=" + $pick.W + "x" + $pick.H2)
